#include "pch.h"
#include "haptics/HapticMixer.h"
#include "haptics/DecisionEngine.h"
#include "haptics/AudioOnlyScorer.h"
#include "haptics/DynamicHapticPool.h"
#include "haptics/HapticsConfig.h"
#include "haptics/HidOutput.h"
#include "haptics/EngineAudioTap.h"
#include "haptics/EventNormalizer.h"
#include "haptics/FootstepAudioMatcher.h"
#include "haptics/FootstepTruthBridge.h"
#include "haptics/FootstepTruthProbe.h"
#include "haptics/HapticEligibilityEngine.h"
#include "haptics/MetricsReporter.h"
#include "haptics/PlayPathHook.h"
#include "haptics/SemanticResolver.h"
#include "haptics/SessionFormPromoter.h"
#include "haptics/VoiceManager.h"

#include <SKSE/SKSE.h>
#include <array>
#include <algorithm>
#include <chrono>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr bool kVerboseSourceLogs = false;
        constexpr float kUnknownSoftCap = 0.08f;
        constexpr float kUnknownSoftConfidenceCap = 0.30f;
        constexpr float kAudioLockBlendAlpha = 0.45f;
        constexpr std::uint32_t kUnknownStructuredBudgetPerSec = 120;
        constexpr std::uint32_t kUnknownUnstructuredBudgetPerSec = 60;
        constexpr std::uint32_t kOutputCarrierMs = 110;
        constexpr float kNonZeroSignalFloor = 0.002f;
        constexpr float kEventLeaseSignalFloor = 0.006f;

        bool IsBackgroundEvent(EventType type)
        {
            return type == EventType::Ambient ||
                type == EventType::Music ||
                type == EventType::UI;
        }

        bool IsL1BackedSource(const HapticSourceMsg& source)
        {
            return (source.flags & HapticSourceFlagL1Trace) != 0;
        }

        std::uint64_t MixKey64(std::uint64_t v)
        {
            v ^= (v >> 33);
            v *= 0xff51afd7ed558ccdULL;
            v ^= (v >> 33);
            v *= 0xc4ceb9fe1a85ec53ULL;
            v ^= (v >> 33);
            return v;
        }

        std::uint64_t BuildAudioLockKey(const HapticSourceMsg& source)
        {
            if (source.type != SourceType::AudioMod || source.sourceVoiceId == 0) {
                return 0;
            }

            if (source.eventType == EventType::Unknown &&
                ((source.flags & HapticSourceFlagSessionPromoted) == 0)) {
                return 0;
            }

            std::uint64_t key = MixKey64(source.sourceVoiceId);
            const auto formAndEvent =
                (static_cast<std::uint64_t>(source.sourceFormId) << 8u) |
                static_cast<std::uint64_t>(source.eventType);
            key ^= MixKey64(formAndEvent + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2));
            return MixKey64(key);
        }

        bool CanStartAudioLock(const HapticSourceMsg& source, const HapticsConfig& cfg)
        {
            const auto conf = std::clamp(source.confidence, 0.0f, 1.0f);
            if (IsL1BackedSource(source) &&
                source.sourceFormId != 0 &&
                source.eventType != EventType::Unknown) {
                return true;
            }

            if (source.eventType == EventType::Unknown) {
                if ((source.flags & HapticSourceFlagSessionPromoted) == 0) {
                    return false;
                }

                // Structured unknown (has form context) should not use the stricter
                // foreground threshold, otherwise lock binding almost never starts.
                if (source.sourceFormId != 0) {
                    const float structuredUnknownStart = std::min(
                        cfg.audioLockUnknownStartMinConfidence,
                        std::max(0.0f, cfg.audioLockStartMinConfidence - 0.10f));
                    return conf >= structuredUnknownStart;
                }

                return conf >= cfg.audioLockUnknownStartMinConfidence;
            }

            if (source.sourceFormId != 0 || source.eventType != EventType::Unknown) {
                return conf >= cfg.audioLockStartMinConfidence;
            }
            return conf >= cfg.audioLockStartMinConfidence;
        }

        bool CanExtendAudioLock(const HapticSourceMsg& source, const HapticsConfig& cfg)
        {
            const auto conf = std::clamp(source.confidence, 0.0f, 1.0f);
            if (IsL1BackedSource(source) &&
                source.sourceFormId != 0 &&
                source.eventType != EventType::Unknown) {
                return true;
            }

            if (source.eventType == EventType::Unknown) {
                if ((source.flags & HapticSourceFlagSessionPromoted) == 0) {
                    return false;
                }
                if (source.sourceFormId == 0) {
                    return false;
                }

                const auto unknownExtendMin = std::max(
                    cfg.audioLockExtendMinConfidence,
                    std::max(0.0f, cfg.audioLockUnknownStartMinConfidence - 0.28f));
                return conf >= unknownExtendMin;
            }

            return conf >= cfg.audioLockExtendMinConfidence;
        }

        enum class ForegroundFamily : std::uint8_t
        {
            Unknown = 0,
            Impact,
            Swing,
            Footstep,
            Voice,
            Other,
            Count
        };

        ForegroundFamily ClassifyForegroundFamily(EventType type)
        {
            switch (type) {
            case EventType::HitImpact:
            case EventType::Block:
                return ForegroundFamily::Impact;
            case EventType::WeaponSwing:
            case EventType::BowRelease:
            case EventType::SpellCast:
            case EventType::SpellImpact:
                return ForegroundFamily::Swing;
            case EventType::Footstep:
            case EventType::Jump:
            case EventType::Land:
                return ForegroundFamily::Footstep;
            case EventType::Shout:
                return ForegroundFamily::Voice;
            case EventType::Unknown:
                return ForegroundFamily::Unknown;
            default:
                return ForegroundFamily::Other;
            }
        }

        std::uint32_t GetMinSustainMs(const HapticSourceMsg& source)
        {
            switch (source.eventType) {
            case EventType::HitImpact:
            case EventType::Block:
                return 70u;
            case EventType::WeaponSwing:
            case EventType::BowRelease:
            case EventType::SpellCast:
            case EventType::SpellImpact:
                return 88u;
            case EventType::Footstep:
            case EventType::Jump:
            case EventType::Land:
                return 52u;
            case EventType::Shout:
                return 95u;
            case EventType::Unknown:
                return (source.sourceFormId != 0) ? 58u : 42u;
            default:
                return 60u;
            }
        }

        std::uint32_t GetReleaseMs(const HapticSourceMsg& source)
        {
            switch (source.eventType) {
            case EventType::HitImpact:
            case EventType::Block:
                return 52u;
            case EventType::WeaponSwing:
            case EventType::BowRelease:
            case EventType::SpellCast:
            case EventType::SpellImpact:
                return 66u;
            case EventType::Footstep:
            case EventType::Jump:
            case EventType::Land:
                return 42u;
            case EventType::Shout:
                return 72u;
            case EventType::Unknown:
                return (source.sourceFormId != 0) ? 48u : 34u;
            default:
                return 44u;
            }
        }

        std::uint32_t GetEventLeaseHoldMs(EventType type, bool structured)
        {
            switch (type) {
            case EventType::HitImpact:
            case EventType::Block:
                return 96u;
            case EventType::WeaponSwing:
            case EventType::BowRelease:
            case EventType::SpellCast:
            case EventType::SpellImpact:
                return 112u;
            case EventType::Footstep:
            case EventType::Jump:
            case EventType::Land:
                return 72u;
            case EventType::Shout:
                return 120u;
            case EventType::Unknown:
                return structured ? 76u : 0u;
            default:
                return 84u;
            }
        }

        std::uint32_t GetEventLeaseReleaseMs(EventType type, bool structured)
        {
            switch (type) {
            case EventType::HitImpact:
            case EventType::Block:
                return 74u;
            case EventType::WeaponSwing:
            case EventType::BowRelease:
            case EventType::SpellCast:
            case EventType::SpellImpact:
                return 86u;
            case EventType::Footstep:
            case EventType::Jump:
            case EventType::Land:
                return 56u;
            case EventType::Shout:
                return 92u;
            case EventType::Unknown:
                return structured ? 60u : 0u;
            default:
                return 68u;
            }
        }

        struct PeriodicLogSnapshot
        {
            EngineAudioTap::Stats tap{};
            AudioOnlyScorer::Stats audioOnly{};
            HapticMixer::Stats mixer{};
            DecisionEngine::Stats decision{};
            HidOutput::Stats hid{};
            VoiceManager::Stats voice{};
            PlayPathHook::Stats playPath{};
            EventNormalizer::Stats normalizer{};
            DynamicHapticPool::Stats dynamicPool{};
            SessionFormPromoter::Stats sessionPromoter{};
            HapticEligibilityEngine::Stats eligibility{};
            MetricsReporter::Snapshot metrics{};
            FootstepTruthProbe::Stats footTruth{};
            FootstepAudioMatcher::Stats footAudio{};
            FootstepTruthBridge::Stats footBridge{};
        };

        PeriodicLogSnapshot CollectPeriodicLogSnapshot(HapticMixer& mixer)
        {
            PeriodicLogSnapshot snap{};
            snap.tap = EngineAudioTap::GetStats();
            snap.audioOnly = AudioOnlyScorer::GetSingleton().GetStats();
            snap.mixer = mixer.GetStats();
            snap.decision = DecisionEngine::GetSingleton().GetStats();
            snap.hid = HidOutput::GetSingleton().GetStats();
            snap.voice = VoiceManager::GetSingleton().GetStats();
            snap.playPath = PlayPathHook::GetSingleton().GetStats();
            snap.normalizer = EventNormalizer::GetSingleton().GetStats();
            snap.dynamicPool = DynamicHapticPool::GetSingleton().GetStats();
            snap.sessionPromoter = SessionFormPromoter::GetSingleton().GetStats();
            snap.eligibility = HapticEligibilityEngine::GetSingleton().GetStats();
            snap.metrics = MetricsReporter::GetSingleton().SnapshotAndReset(
                VoiceManager::GetSingleton().GetQueueSize(),
                snap.voice.featuresDropped);
            snap.footTruth = FootstepTruthProbe::GetSingleton().GetStats();
            snap.footAudio = FootstepAudioMatcher::GetSingleton().GetStats();
            snap.footBridge = FootstepTruthBridge::GetSingleton().GetStats();
            return snap;
        }

        void LogPeriodicStats(const PeriodicLogSnapshot& s)
        {
            logger::info(
                "[Haptics][AudioOnly] SubmitTap(calls={} pushed={} skipCmp={}) "
                "PlayPath(init={} initRes={} submit={} submitRes={} skipRes={} retry={} skipRL={} skipMax={} scan={} retryRes={} noForm1={} noFormR={} noCtxScan={} noCtxRes={} noCtxNoPtr={} noCtxDeepScan={} noCtxDeepRes={} skipInit={} skipSub={} skipOth={} traceHit={} traceMiss={} upsert={} bindMiss={}) "
                "Normalize(noVoice={} bindMiss={} traceMiss={} traceHit={} patchForm={} patchEvt={} patchEvtCons={} unkAfter={} unkForm={} unkNoForm={} unkMapOv={}) "
                "AudioOnly(pulled={} produced={} lowDrop={} relDrop={}) "
                "Decision(l1={} l2={} l3={} noCand={} lowFb={} dynHit={} dynMiss={}) "
                "Reason(l2High={} l2Mid={} l2LowPass={}) "
                "Reject(normNoVoice={} normBindMiss={} normTraceMiss={} semNoForm={} semCacheMiss={} semLowConf={} traceUnbound={} traceExpired={} traceDisabled={}) "
                "Gate(acc={} rej={} unk={} bg={} noTrace={} lowSem={} lowRel={} refr={}) "
                "DynLearn(l2={} noKey={} lowScore={} l3DropWeakUnk={}) "
                "SessPromo(obs={} acc={} try={} hit={} miss={} ent={}) "
                "FormSemantic(hit={} miss={} noForm={} cacheMiss={} lowConf={}) "
                "DynPool(size={} admit={} rejKey={} rejLow={} shCall={} shHit={} shMiss={} hit={} miss={} rejMinHit={} rejLowIn={} evict={}) "
                "Metrics(latP50={}us latP95={}us samples={} unknown={:.2f} metaMis={:.2f} qDepth={} drop={}) "
                "Trace(bindHit={}) "
                "Budget(activeFg={} activeBg={} drop={}) "
                "Mixer(active={} frames={} ticks={} src={} clampUnk={} clampBg={} bgIso={} fgFam={} dropEvt={} dropUnkLow={} dropUnkSem={} aLockMerge={} aLockNew={} aLockRej={} meta(active/carrier={}/{}) dropOutUnk={}) "
                "HID(frames={} fail={} ok={} noDev={} writeFail={}) "
                "Voice(drop={}) NoCandSplit(tickNoAudio={} audioNoMatch={})",
                s.tap.submitCalls,
                s.tap.submitFeaturesPushed,
                s.tap.submitCompressedSkipped,
                s.playPath.initCalls,
                s.playPath.initResolved,
                s.playPath.submitCalls,
                s.playPath.submitResolved,
                s.playPath.submitSkipResolved,
                s.playPath.submitRetryScans,
                s.playPath.submitSkipRateLimit,
                s.playPath.submitSkipMaxAttempts,
                s.playPath.submitScanExecuted,
                s.playPath.submitResolvedOnRetry,
                s.playPath.submitNoFormFirstScan,
                s.playPath.submitNoFormRetry,
                s.playPath.submitNoContextScan,
                s.playPath.submitNoContextResolved,
                s.playPath.submitNoContextNoInitPtr,
                s.playPath.submitNoContextDeepScan,
                s.playPath.submitNoContextDeepResolved,
                s.playPath.submitSkipResolvedFromInit,
                s.playPath.submitSkipResolvedFromSubmit,
                s.playPath.submitSkipResolvedOther,
                s.playPath.submitTraceMetaHit,
                s.playPath.submitTraceMetaMiss,
                s.playPath.traceUpserts,
                s.playPath.bindingMisses,
                s.normalizer.noVoiceID,
                s.normalizer.bindingMiss,
                s.normalizer.traceMiss,
                s.normalizer.traceHit,
                s.normalizer.patchedFormID,
                s.normalizer.patchedEventType,
                s.normalizer.patchedEventTypeConservative,
                s.normalizer.unknownAfterNormalize,
                s.normalizer.unknownWithFormID,
                s.normalizer.unknownNoFormID,
                s.normalizer.unknownMapOverflow,
                s.audioOnly.featuresPulled,
                s.audioOnly.sourcesProduced,
                s.audioOnly.lowEnergyDropped,
                s.audioOnly.relativeEnergyDropped,
                s.decision.l1Count,
                s.decision.l2Count,
                s.decision.l3Count,
                s.decision.noCandidate,
                s.decision.lowScoreFallback,
                s.decision.dynamicPoolHit,
                s.decision.dynamicPoolMiss,
                s.decision.l2HighScore,
                s.decision.l2MidScore,
                s.decision.l2LowScorePass,
                s.normalizer.noVoiceID,
                s.normalizer.bindingMiss,
                s.normalizer.traceMiss,
                s.decision.l1FormSemanticNoFormID,
                s.decision.l1FormSemanticCacheMiss,
                s.decision.l1FormSemanticLowConfidence,
                s.decision.traceBindMissUnbound,
                s.decision.traceBindMissExpired,
                s.decision.traceBindBypassDisabled,
                s.decision.gateAccepted,
                s.decision.gateRejected,
                s.decision.rejectUnknownBlocked,
                s.decision.rejectBackgroundBlocked,
                s.decision.rejectNoTraceContext,
                s.decision.rejectLowSemanticConfidence,
                s.decision.rejectLowRelativeEnergy,
                s.decision.rejectRefractoryBlocked,
                s.decision.dynamicPoolLearnFromL2,
                s.decision.dynamicPoolLearnFromL2NoKey,
                s.decision.dynamicPoolLearnFromL2LowScore,
                s.decision.l3DroppedUnstructuredWeakUnknown,
                s.sessionPromoter.observeCalls,
                s.sessionPromoter.observedAccepted,
                s.sessionPromoter.promoteCalls,
                s.sessionPromoter.promoteHits,
                s.sessionPromoter.promoteMisses,
                s.sessionPromoter.entries,
                s.decision.l1FormSemanticHit,
                s.decision.l1FormSemanticMiss,
                s.decision.l1FormSemanticNoFormID,
                s.decision.l1FormSemanticCacheMiss,
                s.decision.l1FormSemanticLowConfidence,
                s.dynamicPool.currentSize,
                s.dynamicPool.admitted,
                s.dynamicPool.rejectedNoKey,
                s.dynamicPool.rejectedLowConfidence,
                s.dynamicPool.shadowCalls,
                s.dynamicPool.shadowHits,
                s.dynamicPool.shadowMisses,
                s.dynamicPool.resolveHits,
                s.dynamicPool.resolveMisses,
                s.dynamicPool.resolveRejectMinHits,
                s.dynamicPool.resolveRejectLowInput,
                s.dynamicPool.evicted,
                s.metrics.latencyP50Us,
                s.metrics.latencyP95Us,
                s.metrics.sampleCount,
                s.metrics.unknownRatio,
                s.metrics.metaMismatchRatio,
                s.metrics.queueDepth,
                s.metrics.dropCount,
                s.decision.traceBindHit,
                s.mixer.activeForeground,
                s.mixer.activeBackground,
                s.mixer.budgetDropCount,
                s.mixer.activeSources,
                s.mixer.framesOutput,
                s.mixer.totalTicks,
                s.mixer.totalSourcesAdded,
                s.mixer.softClampUnknown,
                s.mixer.softClampBackground,
                s.mixer.dropBackgroundHardIsolated,
                s.mixer.foregroundFamilies,
                s.mixer.dropEventDisabled,
                s.mixer.dropUnknownLowInput,
                s.mixer.dropUnknownSemantic,
                s.mixer.audioLockMerged,
                s.mixer.audioLockCreated,
                s.mixer.audioLockRejectedStart,
                s.mixer.outputMetaFromActive,
                s.mixer.outputMetaFromCarrier,
                s.mixer.outputDropUnknownNonZero,
                s.hid.totalFramesSent,
                s.hid.sendFailures,
                s.hid.sendWriteOk,
                s.hid.sendNoDevice,
                s.hid.sendWriteFail,
                s.voice.featuresDropped,
                s.decision.tickNoAudio,
                s.decision.audioPresentNoMatch);

            if (s.decision.gateRejected > 0 ||
                s.eligibility.refractoryWindowHit > 0 ||
                s.eligibility.refractorySoftSuppressed > 0 ||
                s.eligibility.refractoryHardDropped > 0) {
                logger::info(
                    "[Haptics][ProbeGate] rej={} unk={} bg={} noTrace={} lowSem={} lowRel={} refrHard={} refrWindow={} refrSoft={}",
                    s.decision.gateRejected,
                    s.decision.rejectUnknownBlocked,
                    s.decision.rejectBackgroundBlocked,
                    s.decision.rejectNoTraceContext,
                    s.decision.rejectLowSemanticConfidence,
                    s.decision.rejectLowRelativeEnergy,
                    s.eligibility.refractoryHardDropped,
                    s.eligibility.refractoryWindowHit,
                    s.eligibility.refractorySoftSuppressed);
            }

            if (s.mixer.sourceDropUnknownBudget > 0 ||
                s.mixer.sourceLateRescue > 0 ||
                s.mixer.budgetDropCount > 0 ||
                s.mixer.sourceAge100MsPlus > 0) {
                logger::info(
                    "[Haptics][ProbeMixer] add(call={} ins={} mergeForm={} mergeLock={} dropUnkBudget={} lateRescue={} bgIso={}) age(sample={} <8ms={} 8-20={} 20-50={} 50-100={} 100+={} max={}us) budget(unk={} fg={} bg={} total={} fgFamilies={})",
                    s.mixer.sourceAddCalls,
                    s.mixer.sourceInserted,
                    s.mixer.sourceMergedSameForm,
                    s.mixer.sourceMergedAudioLock,
                    s.mixer.sourceDropUnknownBudget,
                    s.mixer.sourceLateRescue,
                    s.mixer.dropBackgroundHardIsolated,
                    s.mixer.sourceAgeSamples,
                    s.mixer.sourceAgeLt8Ms,
                    s.mixer.sourceAge8To20Ms,
                    s.mixer.sourceAge20To50Ms,
                    s.mixer.sourceAge50To100Ms,
                    s.mixer.sourceAge100MsPlus,
                    s.mixer.sourceAgeMaxUs,
                    s.mixer.budgetDropUnknown,
                    s.mixer.budgetDropForeground,
                    s.mixer.budgetDropBackground,
                    s.mixer.budgetDropCount,
                    s.mixer.foregroundFamilies);
            }

            logger::info(
                "[Haptics][Tx] qFg={} qBg={} dqFg={} dqBg={} dropFullFg={} dropFullBg={} dropStaleFg={} dropStaleBg={} salvageFg={} salvageBg={} salvageDrop={} mergeFg={} mergeBg={} sendOk={} sendFail={} noDev={} depthFg={} depthBg={} renderP50={}us renderP95={}us renderN={} firstP50={}us firstP95={}us firstN={} skipRpt={} stopFlush={} route(fg={} bg={} fgZero={} fgHint={} fgPrio={} fgEvt={} bgUnk={} bgEvt={}) select(forceFg={} bgWhileFg={}) flush(capHit={} capDueFg={} capDueBg={} noSelPend={} lookMiss={}) state(upFg={} upBg={} ovFg={} ovBg={} carryQ(FG/BG)={}/{} carryDrop(FG/BG)={}/{} carryUse(FG/BG)={}/{} expDrop={} futSkip={})",
                s.hid.txQueuedFg,
                s.hid.txQueuedBg,
                s.hid.txDequeuedFg,
                s.hid.txDequeuedBg,
                s.hid.txDropQueueFullFg,
                s.hid.txDropQueueFullBg,
                s.hid.txDropStaleFg,
                s.hid.txDropStaleBg,
                s.hid.txSoftSalvageFg,
                s.hid.txSoftSalvageBg,
                s.hid.txSoftSalvageDropped,
                s.hid.txMergedFg,
                s.hid.txMergedBg,
                s.hid.txSendOk,
                s.hid.txSendFail,
                s.hid.txNoDevice,
                s.hid.txQueueDepthFg,
                s.hid.txQueueDepthBg,
                s.hid.txRenderOverP50Us,
                s.hid.txRenderOverP95Us,
                s.hid.txRenderOverSamples,
                s.hid.txFirstRenderP50Us,
                s.hid.txFirstRenderP95Us,
                s.hid.txFirstRenderSamples,
                s.hid.txSkippedRepeat,
                s.hid.txStopFlushes,
                s.hid.txRouteFg,
                s.hid.txRouteBg,
                s.hid.txRouteFgZero,
                s.hid.txRouteFgHint,
                s.hid.txRouteFgPriority,
                s.hid.txRouteFgEvent,
                s.hid.txRouteBgUnknown,
                s.hid.txRouteBgBackground,
                s.hid.txSelectForcedFgBudget,
                s.hid.txSelectBgWhileFgPending,
                s.hid.txFlushCapHit,
                s.hid.txFlushCapDueFg,
                s.hid.txFlushCapDueBg,
                s.hid.txFlushNoSelectPending,
                s.hid.txFlushLookaheadMiss,
                s.hid.txStateUpdateFg,
                s.hid.txStateUpdateBg,
                s.hid.txStateOverwriteFg,
                s.hid.txStateOverwriteBg,
                s.hid.txStateCarryQueuedFg,
                s.hid.txStateCarryQueuedBg,
                s.hid.txStateCarryDropFg,
                s.hid.txStateCarryDropBg,
                s.hid.txStateCarryConsumedFg,
                s.hid.txStateCarryConsumedBg,
                s.hid.txStateExpiredDrop,
                s.hid.txStateFutureSkip);

            logger::info(
                "[Haptics][FootTruth] total={} player={} admissible={} shadow(match={} truthMiss={} renderMiss={} deltaP50={}us deltaP95={}us samples={} pending={})",
                s.footTruth.totalEvents,
                s.footTruth.playerEvents,
                s.footTruth.admissibleEvents,
                s.footTruth.shadowMatchedRenders,
                s.footTruth.shadowExpiredTruthMisses,
                s.footTruth.shadowRenderWithoutTruth,
                s.footTruth.shadowRenderDeltaP50Us,
                s.footTruth.shadowRenderDeltaP95Us,
                s.footTruth.shadowRenderDeltaSamples,
                s.footTruth.shadowPendingTruth);
            logger::info(
                "[Haptics][FootBridge] truths={} inst={} claims={} init={} submit={} miss(truth/inst)={}/{} deltaP50={}us deltaP95={}us samples={} pending(t/i/b)={}/{}/{}",
                s.footBridge.truthsObserved,
                s.footBridge.instancesObserved,
                s.footBridge.claimsMatched,
                s.footBridge.claimsFromInit,
                s.footBridge.claimsFromSubmit,
                s.footBridge.truthExpiredMisses,
                s.footBridge.instanceExpiredMisses,
                s.footBridge.deltaP50Us,
                s.footBridge.deltaP95Us,
                s.footBridge.deltaSamples,
                s.footBridge.pendingTruths,
                s.footBridge.pendingInstances,
                s.footBridge.activeBindings);
            logger::info(
                "[Haptics][FootAudio] features={} truths={} matched={} bridge(bound/match/noFeat)={}/{}/{} noWin={} noSem={} lowScore={} cand(window/sem)={}/{} miss(bind/trace)={}/{} deltaP50={}us deltaP95={}us scoreP50={:.2f} scoreP95={:.2f} durP50={}us durP95={}us panAbsP50={:.2f} panAbsP95={:.2f} pending={}",
                s.footAudio.featuresObserved,
                s.footAudio.truthsObserved,
                s.footAudio.truthsMatched,
                s.footAudio.truthBridgeBound,
                s.footAudio.truthBridgeMatched,
                s.footAudio.truthBridgeNoFeature,
                s.footAudio.truthNoWindow,
                s.footAudio.truthNoSemantic,
                s.footAudio.truthLowScore,
                s.footAudio.windowCandidates,
                s.footAudio.semanticCandidates,
                s.footAudio.bindingMissCandidates,
                s.footAudio.traceMissCandidates,
                s.footAudio.matchDeltaP50Us,
                s.footAudio.matchDeltaP95Us,
                static_cast<float>(s.footAudio.matchScoreP50Permille) / 1000.0f,
                static_cast<float>(s.footAudio.matchScoreP95Permille) / 1000.0f,
                s.footAudio.matchDurationP50Us,
                s.footAudio.matchDurationP95Us,
                static_cast<float>(s.footAudio.matchPanAbsP50Permille) / 1000.0f,
                static_cast<float>(s.footAudio.matchPanAbsP95Permille) / 1000.0f,
                s.footAudio.pendingTruths);

            const auto fgRouteTotal = s.hid.txRouteFg;
            const auto deqTotal = s.hid.txDequeuedFg + s.hid.txDequeuedBg;
            const auto staleTotal = s.hid.txDropStaleFg + s.hid.txDropStaleBg;
            const float fgHintRatio = (fgRouteTotal > 0) ?
                (100.0f * static_cast<float>(s.hid.txRouteFgHint) / static_cast<float>(fgRouteTotal)) :
                0.0f;
            const float fgEventRatio = (fgRouteTotal > 0) ?
                (100.0f * static_cast<float>(s.hid.txRouteFgEvent) / static_cast<float>(fgRouteTotal)) :
                0.0f;
            const float staleRatio = (deqTotal > 0) ?
                (100.0f * static_cast<float>(staleTotal) / static_cast<float>(deqTotal)) :
                0.0f;

            if (fgRouteTotal >= 256 &&
                (fgHintRatio >= 35.0f ||
                    fgEventRatio <= 8.0f ||
                    staleRatio >= 4.0f ||
                    s.hid.txRenderOverP95Us >= 22000u ||
                    s.hid.txFirstRenderP95Us >= 22000u ||
                    s.hid.txSelectBgWhileFgPending > 0u)) {
                logger::info(
                    "[Haptics][ProbeTxDiag] fgHint={:.1f}% fgEvt={:.1f}% stale={:.1f}% staleFg={} staleBg={} salvageFg={} salvageBg={} renderP95={}us firstP95={}us bgWhileFg={} capHit={} capDueFg={} capDueBg={} noSelPend={} lookMiss={} upFg={} ovFg={} carryQ={} carryDrop={} carryUse={} expDrop={} futSkip={} fgFamilies={} activeFg={} activeBg={} noCand={} audioNoMatch={}",
                    fgHintRatio,
                    fgEventRatio,
                    staleRatio,
                    s.hid.txDropStaleFg,
                    s.hid.txDropStaleBg,
                    s.hid.txSoftSalvageFg,
                    s.hid.txSoftSalvageBg,
                    s.hid.txRenderOverP95Us,
                    s.hid.txFirstRenderP95Us,
                    s.hid.txSelectBgWhileFgPending,
                    s.hid.txFlushCapHit,
                    s.hid.txFlushCapDueFg,
                    s.hid.txFlushCapDueBg,
                    s.hid.txFlushNoSelectPending,
                    s.hid.txFlushLookaheadMiss,
                    s.hid.txStateUpdateFg,
                    s.hid.txStateOverwriteFg,
                    s.hid.txStateCarryQueuedFg + s.hid.txStateCarryQueuedBg,
                    s.hid.txStateCarryDropFg + s.hid.txStateCarryDropBg,
                    s.hid.txStateCarryConsumedFg + s.hid.txStateCarryConsumedBg,
                    s.hid.txStateExpiredDrop,
                    s.hid.txStateFutureSkip,
                    s.mixer.foregroundFamilies,
                    s.mixer.activeForeground,
                    s.mixer.activeBackground,
                    s.decision.tickNoAudio,
                    s.decision.audioPresentNoMatch);
            }
        }
    }

    HapticMixer& HapticMixer::GetSingleton()
    {
        static HapticMixer instance;
        return instance;
    }

    void HapticMixer::Start()
    {
        if (_running.exchange(true)) {
            logger::warn("[Haptics][Mixer] Already running");
            return;
        }

        logger::info("[Haptics][Mixer] Starting mixer thread...");

        _activeSources.reserve(64);
        _lastLeft = 0.0f;
        _lastRight = 0.0f;
        _totalTicks.store(0, std::memory_order_relaxed);
        _totalSourcesAdded.store(0, std::memory_order_relaxed);
        _framesOutput.store(0, std::memory_order_relaxed);
        _softClampUnknown.store(0, std::memory_order_relaxed);
        _softClampBackground.store(0, std::memory_order_relaxed);
        _dropEventDisabled.store(0, std::memory_order_relaxed);
        _dropUnknownLowInput.store(0, std::memory_order_relaxed);
        _dropUnknownSemantic.store(0, std::memory_order_relaxed);
        _budgetDropCount.store(0, std::memory_order_relaxed);
        _activeForeground.store(0, std::memory_order_relaxed);
        _activeBackground.store(0, std::memory_order_relaxed);
        _audioLockMerged.store(0, std::memory_order_relaxed);
        _audioLockCreated.store(0, std::memory_order_relaxed);
        _audioLockRejectedStart.store(0, std::memory_order_relaxed);
        _sourceAddCalls.store(0, std::memory_order_relaxed);
        _sourceInserted.store(0, std::memory_order_relaxed);
        _sourceMergedSameForm.store(0, std::memory_order_relaxed);
        _sourceMergedAudioLock.store(0, std::memory_order_relaxed);
        _sourceDropUnknownBudget.store(0, std::memory_order_relaxed);
        _sourceLateRescue.store(0, std::memory_order_relaxed);
        _sourceAgeSamples.store(0, std::memory_order_relaxed);
        _sourceAgeLt8Ms.store(0, std::memory_order_relaxed);
        _sourceAge8To20Ms.store(0, std::memory_order_relaxed);
        _sourceAge20To50Ms.store(0, std::memory_order_relaxed);
        _sourceAge50To100Ms.store(0, std::memory_order_relaxed);
        _sourceAge100MsPlus.store(0, std::memory_order_relaxed);
        _sourceAgeMaxUs.store(0, std::memory_order_relaxed);
        _budgetDropUnknown.store(0, std::memory_order_relaxed);
        _budgetDropForeground.store(0, std::memory_order_relaxed);
        _budgetDropBackground.store(0, std::memory_order_relaxed);
        _dropBackgroundHardIsolated.store(0, std::memory_order_relaxed);
        _foregroundFamilies.store(0, std::memory_order_relaxed);
        _outputMetaFromActive.store(0, std::memory_order_relaxed);
        _outputMetaFromCarrier.store(0, std::memory_order_relaxed);
        _outputDropUnknownNonZero.store(0, std::memory_order_relaxed);
        _peakLeft.store(0.0f, std::memory_order_relaxed);
        _peakRight.store(0.0f, std::memory_order_relaxed);
        _unknownBudgetWindowUs = 0;
        _unknownStructuredUsed = 0;
        _unknownUnstructuredUsed = 0;
        _probeWindowUs = 0;
        _probeLinesInWindow = 0;
        _outputCarrier = {};
        _eventLease = {};

        auto& config = HapticsConfig::GetSingleton();
        _focusManager.SetDuckingRules(config.duckingRules);

        AudioOnlyScorer::GetSingleton().Initialize();
        DecisionEngine::GetSingleton().Initialize();
        EventNormalizer::GetSingleton().ResetStats();
        SemanticResolver::GetSingleton().ResetStats();
        PlayPathHook::GetSingleton().ResetStats();
        DynamicHapticPool::GetSingleton().Clear();
        DynamicHapticPool::GetSingleton().ResetStats();
        MetricsReporter::GetSingleton().Reset();

        _thread = std::jthread([this](std::stop_token st) {
            (void)st;
            MixerThreadLoop();
            });

        logger::info("[Haptics][Mixer] Mixer thread started");
    }

    void HapticMixer::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        logger::info("[Haptics][Mixer] Stopping mixer thread...");

        if (_thread.joinable()) {
            _thread.request_stop();
            _thread.join();
        }

        DecisionEngine::GetSingleton().Shutdown();
        AudioOnlyScorer::GetSingleton().Shutdown();

        {
            std::scoped_lock lock(_mutex);
            _activeSources.clear();
        }

        logger::info("[Haptics][Mixer] Mixer thread stopped");
    }

    void HapticMixer::AddSource(const HapticSourceMsg& msg)
    {
        if (!_running.load(std::memory_order_acquire)) {
            return;
        }

        auto& config = HapticsConfig::GetSingleton();
        HapticSourceMsg adjusted = msg;

        if (config.IsNativeOnly()) {
            return;
        }

        _sourceAddCalls.fetch_add(1, std::memory_order_relaxed);

        const auto now = Now();
        const auto nowUs = ToQPC(now);
        auto shouldEmitProbeLog = [&](std::uint64_t tsUs) -> bool {
            if (_probeWindowUs == 0 ||
                tsUs < _probeWindowUs ||
                (tsUs - _probeWindowUs) >= 1'000'000ull) {
                _probeWindowUs = tsUs;
                _probeLinesInWindow = 0;
            }
            if (_probeLinesInWindow >= 6) {
                return false;
            }
            ++_probeLinesInWindow;
            return true;
            };

        if (adjusted.type == SourceType::AudioMod &&
            adjusted.qpc != 0 &&
            nowUs >= adjusted.qpc) {
            const auto ageUs64 = nowUs - adjusted.qpc;
            const auto ageUs = static_cast<std::uint32_t>(std::min<std::uint64_t>(ageUs64, 0xFFFFFFFFull));
            _sourceAgeSamples.fetch_add(1, std::memory_order_relaxed);
            if (ageUs < 8'000u) {
                _sourceAgeLt8Ms.fetch_add(1, std::memory_order_relaxed);
            }
            else if (ageUs < 20'000u) {
                _sourceAge8To20Ms.fetch_add(1, std::memory_order_relaxed);
            }
            else if (ageUs < 50'000u) {
                _sourceAge20To50Ms.fetch_add(1, std::memory_order_relaxed);
            }
            else if (ageUs < 100'000u) {
                _sourceAge50To100Ms.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                _sourceAge100MsPlus.fetch_add(1, std::memory_order_relaxed);
            }

            auto prevMax = _sourceAgeMaxUs.load(std::memory_order_relaxed);
            while (ageUs > prevMax &&
                !_sourceAgeMaxUs.compare_exchange_weak(
                    prevMax,
                    ageUs,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
            }
        }

        if (adjusted.type == SourceType::AudioMod) {
            if (adjusted.eventType == EventType::Unknown) {
                if (!config.allowUnknownAudioEvent) {
                    const bool structuredUnknown = (adjusted.sourceFormId != 0);
                    const float ampCap = structuredUnknown ? 0.14f : kUnknownSoftCap;
                    const float confCap = structuredUnknown ? 0.45f : kUnknownSoftConfidenceCap;
                    const int priCap = structuredUnknown ?
                        std::max(10, config.priorityFootstep) :
                        std::max(10, config.priorityAmbient);
                    adjusted.left = std::min(adjusted.left, ampCap);
                    adjusted.right = std::min(adjusted.right, ampCap);
                    adjusted.confidence = std::min(adjusted.confidence, confCap);
                    adjusted.priority = std::min(adjusted.priority, priCap);
                    _softClampUnknown.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // Hard isolate audio background/UI path from main haptics mix.
            if (IsBackgroundEvent(adjusted.eventType)) {
                _dropBackgroundHardIsolated.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }

        if (adjusted.eventType != EventType::Unknown && !config.IsEventAllowed(adjusted.eventType)) {
            _dropEventDisabled.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto sustainMs = std::clamp(
            std::max(adjusted.ttlMs, GetMinSustainMs(adjusted)),
            24u,
            420u);
        const auto releaseMs = std::clamp(GetReleaseMs(adjusted), 16u, 160u);
        auto holdUntil = (adjusted.type == SourceType::BaseEvent) ?
            (now + std::chrono::milliseconds(sustainMs)) :
            (FromQPC(adjusted.qpc) + std::chrono::milliseconds(sustainMs));

        if (holdUntil <= now) {
            _sourceLateRescue.fetch_add(1, std::memory_order_relaxed);
            if (shouldEmitProbeLog(nowUs)) {
                logger::info(
                    "[Haptics][Probe][LateSource] age={}us sustain={}ms release={}ms evt={} form=0x{:08X} conf={:.2f} rel={:.2f} voice=0x{:X}",
                    (adjusted.qpc != 0 && nowUs >= adjusted.qpc) ? (nowUs - adjusted.qpc) : 0ull,
                    sustainMs,
                    releaseMs,
                    ToString(adjusted.eventType),
                    adjusted.sourceFormId,
                    adjusted.confidence,
                    adjusted.relativeEnergy,
                    adjusted.sourceVoiceId);
            }
            holdUntil = now + std::chrono::milliseconds(12);
        }

        auto lockKey = config.enableAudioLockBinding ? BuildAudioLockKey(adjusted) : 0ull;
        bool insertedNewSource = false;
        [[maybe_unused]] bool mergedSource = false;
        const bool isUnknownAudio =
            adjusted.type == SourceType::AudioMod &&
            adjusted.eventType == EventType::Unknown;
        const bool structuredUnknown =
            isUnknownAudio &&
            (adjusted.sourceFormId != 0 ||
                IsL1BackedSource(adjusted) ||
                (adjusted.flags & HapticSourceFlagSessionPromoted) != 0);

        {
            std::scoped_lock lock(_mutex);

            if (isUnknownAudio && adjusted.sourceFormId != 0) {
                auto sameForm = std::find_if(_activeSources.begin(), _activeSources.end(),
                    [&](const ActiveSource& s) {
                        return s.msg.type == SourceType::AudioMod &&
                            s.msg.eventType == EventType::Unknown &&
                            s.msg.sourceFormId == adjusted.sourceFormId;
                    });

                if (sameForm != _activeSources.end()) {
                    if (holdUntil > sameForm->holdUntil) {
                        sameForm->holdUntil = holdUntil;
                    }
                    const auto mergedReleaseEnd = holdUntil + std::chrono::milliseconds(releaseMs);
                    if (mergedReleaseEnd > sameForm->releaseEndTime) {
                        sameForm->releaseEndTime = mergedReleaseEnd;
                    }
                    sameForm->msg.qpc = adjusted.qpc;
                    sameForm->msg.priority = std::max(sameForm->msg.priority, adjusted.priority);
                    sameForm->msg.ttlMs = std::max(sameForm->msg.ttlMs, adjusted.ttlMs);
                    sameForm->msg.flags = static_cast<std::uint8_t>(sameForm->msg.flags | adjusted.flags);
                    sameForm->msg.left = std::clamp(
                        sameForm->msg.left * 0.55f + adjusted.left * 0.45f,
                        0.0f,
                        1.0f);
                    sameForm->msg.right = std::clamp(
                        sameForm->msg.right * 0.55f + adjusted.right * 0.45f,
                        0.0f,
                        1.0f);
                    sameForm->msg.confidence = std::clamp(
                        sameForm->msg.confidence * 0.55f + adjusted.confidence * 0.45f,
                        0.0f,
                        1.0f);
                    sameForm->currentLeft = std::clamp(
                        sameForm->currentLeft * 0.65f + sameForm->msg.left * 0.35f,
                        0.0f,
                        1.0f);
                    sameForm->currentRight = std::clamp(
                        sameForm->currentRight * 0.65f + sameForm->msg.right * 0.35f,
                        0.0f,
                        1.0f);

                    std::stable_sort(_activeSources.begin(), _activeSources.end(),
                        [](const ActiveSource& a, const ActiveSource& b) {
                            return a.msg.priority > b.msg.priority;
                        });
                    mergedSource = true;
                    _sourceMergedSameForm.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }

            if (lockKey != 0) {
                auto it = std::find_if(_activeSources.begin(), _activeSources.end(),
                    [&](const ActiveSource& s) { return s.lockKey == lockKey; });

                if (it != _activeSources.end()) {
                    // Use pre-clamp signal confidence to decide lock extension;
                    // otherwise unknown soft clamp can permanently block lock binding.
                    if (CanExtendAudioLock(msg, config)) {
                        const auto mergedHoldUntil = holdUntil +
                            std::chrono::milliseconds(config.audioLockExtendGraceMs);
                        if (mergedHoldUntil > it->holdUntil) {
                            it->holdUntil = mergedHoldUntil;
                        }
                        const auto mergedReleaseEnd = it->holdUntil + std::chrono::milliseconds(releaseMs);
                        if (mergedReleaseEnd > it->releaseEndTime) {
                            it->releaseEndTime = mergedReleaseEnd;
                        }

                        if (it->msg.sourceFormId == 0 && adjusted.sourceFormId != 0) {
                            it->msg.sourceFormId = adjusted.sourceFormId;
                        }
                        if (it->msg.eventType == EventType::Unknown &&
                            adjusted.eventType != EventType::Unknown) {
                            it->msg.eventType = adjusted.eventType;
                        }

                        it->msg.qpc = adjusted.qpc;
                        it->msg.priority = std::max(it->msg.priority, adjusted.priority);
                        it->msg.ttlMs = std::max(it->msg.ttlMs, adjusted.ttlMs);
                        it->msg.flags = static_cast<std::uint8_t>(it->msg.flags | adjusted.flags);
                        it->msg.left = std::clamp(
                            it->msg.left * (1.0f - kAudioLockBlendAlpha) + adjusted.left * kAudioLockBlendAlpha,
                            0.0f,
                            1.0f);
                        it->msg.right = std::clamp(
                            it->msg.right * (1.0f - kAudioLockBlendAlpha) + adjusted.right * kAudioLockBlendAlpha,
                            0.0f,
                            1.0f);
                        it->msg.confidence = std::clamp(
                            it->msg.confidence * (1.0f - kAudioLockBlendAlpha) + adjusted.confidence * kAudioLockBlendAlpha,
                            0.0f,
                            1.0f);
                        it->currentLeft = std::clamp(
                            it->currentLeft * 0.60f + it->msg.left * 0.40f,
                            0.0f,
                            1.0f);
                        it->currentRight = std::clamp(
                            it->currentRight * 0.60f + it->msg.right * 0.40f,
                            0.0f,
                            1.0f);

                        std::stable_sort(_activeSources.begin(), _activeSources.end(),
                            [](const ActiveSource& a, const ActiveSource& b) {
                                return a.msg.priority > b.msg.priority;
                            });

                        mergedSource = true;
                        _audioLockMerged.fetch_add(1, std::memory_order_relaxed);
                        _sourceMergedAudioLock.fetch_add(1, std::memory_order_relaxed);
                    }

                    if constexpr (kVerboseSourceLogs) {
                        logger::info("[Haptics][Mixer] AudioLock existing key=0x{:X} merged={} evt={} conf={:.2f}",
                            lockKey,
                            mergedSource,
                            ToString(adjusted.eventType),
                            adjusted.confidence);
                    }
                    return;
                }

                // Use pre-clamp signal confidence to decide lock start.
                if (!CanStartAudioLock(msg, config)) {
                    _audioLockRejectedStart.fetch_add(1, std::memory_order_relaxed);
                    if constexpr (kVerboseSourceLogs) {
                        logger::info("[Haptics][Mixer] AudioLock reject-start key=0x{:X} evt={} form=0x{:08X} conf={:.2f} fallback=plain",
                            lockKey,
                            ToString(adjusted.eventType),
                            adjusted.sourceFormId,
                            adjusted.confidence);
                    }
                    // Do not drop haptics when lock-start confidence is low.
                    // Fallback to the plain source path without lock binding.
                    lockKey = 0;
                }
            }

            if (isUnknownAudio) {
                if (_unknownBudgetWindowUs == 0 ||
                    nowUs < _unknownBudgetWindowUs ||
                    (nowUs - _unknownBudgetWindowUs) >= 1000000ull) {
                    _unknownBudgetWindowUs = nowUs;
                    _unknownStructuredUsed = 0;
                    _unknownUnstructuredUsed = 0;
                }

                auto& used = structuredUnknown ? _unknownStructuredUsed : _unknownUnstructuredUsed;
                const auto budget = structuredUnknown ?
                    kUnknownStructuredBudgetPerSec :
                    kUnknownUnstructuredBudgetPerSec;
                if (used >= budget) {
                    _budgetDropCount.fetch_add(1, std::memory_order_relaxed);
                    _budgetDropUnknown.fetch_add(1, std::memory_order_relaxed);
                    _sourceDropUnknownBudget.fetch_add(1, std::memory_order_relaxed);
                    if (shouldEmitProbeLog(nowUs)) {
                        logger::info(
                            "[Haptics][Probe][UnknownBudgetDrop] structured={} budget={} evt={} form=0x{:08X} conf={:.2f} amp={:.2f}",
                            structuredUnknown,
                            budget,
                            ToString(adjusted.eventType),
                            adjusted.sourceFormId,
                            adjusted.confidence,
                            std::max(adjusted.left, adjusted.right));
                    }
                    return;
                }
                ++used;
            }

            ActiveSource source;
            source.msg = adjusted;
            source.holdUntil = (lockKey != 0) ?
                (holdUntil + std::chrono::milliseconds(config.audioLockExtendGraceMs)) :
                holdUntil;
            source.releaseEndTime = source.holdUntil + std::chrono::milliseconds(releaseMs);
            source.currentLeft = adjusted.left * 0.55f;
            source.currentRight = adjusted.right * 0.55f;
            source.lockKey = lockKey;

            auto pos = std::find_if(_activeSources.begin(), _activeSources.end(),
                [&](const ActiveSource& a) { return a.msg.priority < source.msg.priority; });
            _activeSources.insert(pos, std::move(source));
            insertedNewSource = true;
            _sourceInserted.fetch_add(1, std::memory_order_relaxed);
            if (lockKey != 0) {
                _audioLockCreated.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (insertedNewSource) {
            _totalSourcesAdded.fetch_add(1, std::memory_order_relaxed);
        }

        if constexpr (kVerboseSourceLogs) {
            logger::info("[Haptics][Mixer] Source added: srcType={} eventType={} L={:.3f} R={:.3f} priority={} lockKey=0x{:X}",
                static_cast<int>(msg.type), ToString(msg.eventType), msg.left, msg.right, msg.priority, lockKey);
        }
    }

    HapticMixer::Stats HapticMixer::GetStats() const
    {
        Stats s;
        s.totalTicks = _totalTicks.load(std::memory_order_relaxed);
        s.totalSourcesAdded = _totalSourcesAdded.load(std::memory_order_relaxed);
        s.framesOutput = _framesOutput.load(std::memory_order_relaxed);
        s.softClampUnknown = _softClampUnknown.load(std::memory_order_relaxed);
        s.softClampBackground = _softClampBackground.load(std::memory_order_relaxed);
        s.dropEventDisabled = _dropEventDisabled.load(std::memory_order_relaxed);
        s.dropUnknownLowInput = _dropUnknownLowInput.load(std::memory_order_relaxed);
        s.dropUnknownSemantic = _dropUnknownSemantic.load(std::memory_order_relaxed);
        s.budgetDropCount = _budgetDropCount.load(std::memory_order_relaxed);
        s.activeForeground = _activeForeground.load(std::memory_order_relaxed);
        s.activeBackground = _activeBackground.load(std::memory_order_relaxed);
        s.audioLockMerged = _audioLockMerged.load(std::memory_order_relaxed);
        s.audioLockCreated = _audioLockCreated.load(std::memory_order_relaxed);
        s.audioLockRejectedStart = _audioLockRejectedStart.load(std::memory_order_relaxed);
        s.sourceAddCalls = _sourceAddCalls.load(std::memory_order_relaxed);
        s.sourceInserted = _sourceInserted.load(std::memory_order_relaxed);
        s.sourceMergedSameForm = _sourceMergedSameForm.load(std::memory_order_relaxed);
        s.sourceMergedAudioLock = _sourceMergedAudioLock.load(std::memory_order_relaxed);
        s.sourceDropUnknownBudget = _sourceDropUnknownBudget.load(std::memory_order_relaxed);
        s.sourceLateRescue = _sourceLateRescue.load(std::memory_order_relaxed);
        s.sourceAgeSamples = _sourceAgeSamples.load(std::memory_order_relaxed);
        s.sourceAgeLt8Ms = _sourceAgeLt8Ms.load(std::memory_order_relaxed);
        s.sourceAge8To20Ms = _sourceAge8To20Ms.load(std::memory_order_relaxed);
        s.sourceAge20To50Ms = _sourceAge20To50Ms.load(std::memory_order_relaxed);
        s.sourceAge50To100Ms = _sourceAge50To100Ms.load(std::memory_order_relaxed);
        s.sourceAge100MsPlus = _sourceAge100MsPlus.load(std::memory_order_relaxed);
        s.sourceAgeMaxUs = _sourceAgeMaxUs.load(std::memory_order_relaxed);
        s.budgetDropUnknown = _budgetDropUnknown.load(std::memory_order_relaxed);
        s.budgetDropForeground = _budgetDropForeground.load(std::memory_order_relaxed);
        s.budgetDropBackground = _budgetDropBackground.load(std::memory_order_relaxed);
        s.dropBackgroundHardIsolated = _dropBackgroundHardIsolated.load(std::memory_order_relaxed);
        s.foregroundFamilies = _foregroundFamilies.load(std::memory_order_relaxed);
        s.outputMetaFromActive = _outputMetaFromActive.load(std::memory_order_relaxed);
        s.outputMetaFromCarrier = _outputMetaFromCarrier.load(std::memory_order_relaxed);
        s.outputDropUnknownNonZero = _outputDropUnknownNonZero.load(std::memory_order_relaxed);
        s.peakLeft = _peakLeft.load(std::memory_order_relaxed);
        s.peakRight = _peakRight.load(std::memory_order_relaxed);

        {
            std::scoped_lock lock(_mutex);
            s.activeSources = static_cast<std::uint32_t>(_activeSources.size());
        }

        s.avgTickTimeUs = 0.0f;
        return s;
    }

    void HapticMixer::MixerThreadLoop()
    {
        auto& config = HapticsConfig::GetSingleton();
        const auto tickDuration = std::chrono::milliseconds(config.tickMs);

        logger::info("[Haptics][Mixer] Mixer loop started (tick={}ms)", config.tickMs);

        auto nextTick = std::chrono::steady_clock::now();
        auto nextStatsLog = std::chrono::steady_clock::now() + std::chrono::seconds(1);

        while (_running.load(std::memory_order_acquire)) {
            HidFrame frame = ProcessTick();

            HidOutput::GetSingleton().SendFrame(frame);
            FootstepTruthBridge::GetSingleton().Tick(ToQPC(Now()));
            FootstepAudioMatcher::GetSingleton().Tick(ToQPC(Now()));

            _totalTicks.fetch_add(1, std::memory_order_relaxed);
            _framesOutput.fetch_add(1, std::memory_order_relaxed);

            _peakLeft.store(std::max(_peakLeft.load(), frame.leftMotor / 255.0f), std::memory_order_relaxed);
            _peakRight.store(std::max(_peakRight.load(), frame.rightMotor / 255.0f), std::memory_order_relaxed);

            nextTick += tickDuration;
            std::this_thread::sleep_until(nextTick);

            auto now = std::chrono::steady_clock::now();
            if (now > nextTick + tickDuration * 2) {
                nextTick = now;
            }

            if (now >= nextStatsLog) {
                nextStatsLog = now + std::chrono::seconds(1);
                LogPeriodicStats(CollectPeriodicLogSnapshot(*this));
            }
        }

        logger::info("[Haptics][Mixer] Mixer loop stopped");
    }

    HidFrame HapticMixer::ProcessTick()
    {
        _focusManager.Update();

        auto decisions = DecisionEngine::GetSingleton().Update();
        MetricsReporter::GetSingleton().OnDecisions(decisions, ToQPC(Now()));
        for (auto& d : decisions) {
            AddSource(d.source);
        }

        UpdateActiveSources();

        float left = 0.0f;
        float right = 0.0f;
        FrameAttribution attribution{};
        MixSources(left, right, &attribution);

        auto now = Now();
        auto nowQpc = ToQPC(now);
        RefreshEventLease(attribution, left, right, nowQpc);
        ApplyEventLease(left, right, attribution, nowQpc);

        ApplyDucking(left, right);
        ApplyCompressor(left, right);
        ApplyLimiter(left, right);
        ApplySlewLimit(left, right);
        ApplyDeadzone(left, right);
        now = Now();
        nowQpc = ToQPC(now);
        const bool nonZeroSignal = (left > kNonZeroSignalFloor || right > kNonZeroSignalFloor);

        EventType dominantEvent = EventType::Unknown;
        SourceType dominantSource = SourceType::AudioMod;
        float dominantConfidence = 0.0f;
        std::uint8_t dominantPriority = 0;
        std::uint32_t dominantSourceFormId = 0;
        std::uint8_t dominantFlags = HapticSourceFlagNone;

        if (attribution.valid) {
            dominantEvent = attribution.eventType;
            dominantSource = attribution.sourceType;
            dominantConfidence = std::clamp(attribution.confidence, 0.0f, 1.0f);
            dominantPriority = attribution.priority;
            dominantSourceFormId = attribution.sourceFormId;
            dominantFlags = attribution.flags;
            _outputMetaFromActive.fetch_add(1, std::memory_order_relaxed);

            const bool attributionStructured =
                attribution.sourceFormId != 0 ||
                (attribution.flags & HapticSourceFlagL1Trace) != 0 ||
                (attribution.flags & HapticSourceFlagSessionPromoted) != 0;
            if (attribution.eventType != EventType::Unknown || attributionStructured) {
                _outputCarrier.valid = true;
                _outputCarrier.eventType = attribution.eventType;
                _outputCarrier.sourceType = attribution.sourceType;
                _outputCarrier.confidence = dominantConfidence;
                _outputCarrier.priority = attribution.priority;
                _outputCarrier.sourceFormId = attribution.sourceFormId;
                _outputCarrier.flags = attribution.flags;
                _outputCarrier.expireUs = nowQpc + static_cast<std::uint64_t>(kOutputCarrierMs) * 1000ull;
            }
        }
        else if (nonZeroSignal &&
            _outputCarrier.valid &&
            nowQpc <= _outputCarrier.expireUs) {
            dominantEvent = _outputCarrier.eventType;
            dominantSource = _outputCarrier.sourceType;
            dominantConfidence = std::clamp(_outputCarrier.confidence, 0.0f, 1.0f);
            dominantPriority = _outputCarrier.priority;
            dominantSourceFormId = _outputCarrier.sourceFormId;
            dominantFlags = _outputCarrier.flags;
            _outputMetaFromCarrier.fetch_add(1, std::memory_order_relaxed);
        }

        const bool hasStructuredContext =
            dominantSourceFormId != 0 ||
            (dominantFlags & HapticSourceFlagL1Trace) != 0 ||
            (dominantFlags & HapticSourceFlagSessionPromoted) != 0;

        if (nonZeroSignal &&
            (dominantEvent == EventType::Unknown && !hasStructuredContext)) {
            const auto droppedLeft = left;
            const auto droppedRight = right;
            left = 0.0f;
            right = 0.0f;
            dominantConfidence = 0.0f;
            dominantPriority = 0;
            dominantEvent = EventType::Unknown;
            dominantSource = SourceType::AudioMod;
            _outputDropUnknownNonZero.fetch_add(1, std::memory_order_relaxed);

            static std::uint64_t s_dropWindowUs = 0;
            static std::uint32_t s_dropWindowLines = 0;
            if (s_dropWindowUs == 0 ||
                nowQpc < s_dropWindowUs ||
                (nowQpc - s_dropWindowUs) >= 1'000'000ull) {
                s_dropWindowUs = nowQpc;
                s_dropWindowLines = 0;
            }
            if (s_dropWindowLines < 6) {
                ++s_dropWindowLines;
                logger::info(
                    "[Haptics][ProbeOutGate] dropUnknownNonZero left={:.3f} right={:.3f} carrierValid={} carrierAge={}us",
                    droppedLeft,
                    droppedRight,
                    _outputCarrier.valid ? 1 : 0,
                    (_outputCarrier.valid && _outputCarrier.expireUs > nowQpc) ?
                    (_outputCarrier.expireUs - nowQpc) : 0ull);
            }
        }

        HidFrame frame{};
        auto& config = HapticsConfig::GetSingleton();
        frame.qpc = nowQpc;
        const auto baseLookaheadUs = config.hidSchedulerLookaheadUs;
        std::uint64_t adaptiveLeadUs = 0;
        if (dominantEvent != EventType::Unknown &&
            !IsBackgroundEvent(dominantEvent)) {
            adaptiveLeadUs = std::min<std::uint64_t>(
                1200ull,
                static_cast<std::uint64_t>(baseLookaheadUs / 2u) + 400ull);
        }
        else if (hasStructuredContext && nonZeroSignal) {
            adaptiveLeadUs = std::min<std::uint64_t>(
                700ull,
                static_cast<std::uint64_t>(baseLookaheadUs / 3u) + 220ull);
        }
        frame.qpcTarget = nowQpc + baseLookaheadUs + adaptiveLeadUs;
        frame.eventType = dominantEvent;
        frame.sourceType = dominantSource;
        frame.confidence = dominantConfidence;
        frame.priority = dominantPriority;
        frame.foregroundHint = (dominantEvent != EventType::Unknown && !IsBackgroundEvent(dominantEvent));
        frame.leftMotor = static_cast<std::uint8_t>(std::clamp(left * 255.0f, 0.0f, 255.0f));
        frame.rightMotor = static_cast<std::uint8_t>(std::clamp(right * 255.0f, 0.0f, 255.0f));
        return frame;
    }

    void HapticMixer::UpdateActiveSources()
    {
        const auto now = Now();

        std::scoped_lock lock(_mutex);
        auto it = _activeSources.begin();
        while (it != _activeSources.end()) {
            if (now >= it->releaseEndTime) {
                it = _activeSources.erase(it);
                continue;
            }

            float gain = 1.0f;
            if (now > it->holdUntil) {
                const auto releaseUs = std::max<std::int64_t>(
                    1,
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        it->releaseEndTime - it->holdUntil)
                        .count());
                const auto elapsedUs = std::max<std::int64_t>(
                    0,
                    std::chrono::duration_cast<std::chrono::microseconds>(now - it->holdUntil).count());
                gain = std::clamp(
                    1.0f - (static_cast<float>(elapsedUs) / static_cast<float>(releaseUs)),
                    0.0f,
                    1.0f);
            }

            const float targetLeft = std::clamp(it->msg.left * gain, 0.0f, 1.0f);
            const float targetRight = std::clamp(it->msg.right * gain, 0.0f, 1.0f);
            const float alpha = (now > it->holdUntil) ? 0.30f : 0.65f;
            it->currentLeft = std::clamp(
                it->currentLeft + (targetLeft - it->currentLeft) * alpha,
                0.0f,
                1.0f);
            it->currentRight = std::clamp(
                it->currentRight + (targetRight - it->currentRight) * alpha,
                0.0f,
                1.0f);
            ++it;
        }
    }

    void HapticMixer::MixSources(float& outLeft, float& outRight, FrameAttribution* outAttribution)
    {
        outLeft = 0.0f;
        outRight = 0.0f;
        _activeForeground.store(0, std::memory_order_relaxed);
        _activeBackground.store(0, std::memory_order_relaxed);
        _foregroundFamilies.store(0, std::memory_order_relaxed);
        if (outAttribution) {
            *outAttribution = {};
        }

        auto& config = HapticsConfig::GetSingleton();
        std::scoped_lock lock(_mutex);
        if (_activeSources.empty()) {
            return;
        }

        struct WeightedSource
        {
            float left{ 0.0f };
            float right{ 0.0f };
            float weight{ 0.0f };
            float score{ 0.0f };
            EventType eventType{ EventType::Unknown };
            SourceType sourceType{ SourceType::AudioMod };
            float confidence{ 0.0f };
            std::uint8_t priority{ 0 };
            std::uint32_t sourceFormId{ 0 };
            std::uint8_t flags{ HapticSourceFlagNone };
        };

        std::vector<WeightedSource> foreground;
        foreground.reserve(static_cast<std::size_t>(ForegroundFamily::Count));
        std::array<WeightedSource, static_cast<std::size_t>(ForegroundFamily::Count)> familyAgg{};
        std::array<bool, static_cast<std::size_t>(ForegroundFamily::Count)> familyUsed{};
        std::uint32_t backgroundIsolated = 0;

        for (const auto& src : _activeSources) {
            float duckFactor = 1.0f;

            if (_focusManager.HasFocus()) {
                EventType srcType = src.msg.eventType;
                EventType focusType = _focusManager.GetCurrentFocus();
                if (srcType != focusType) {
                    duckFactor = _focusManager.GetDuckFactorFor(srcType);
                }
            }

            const float weight = src.msg.confidence * duckFactor;
            if (weight <= 0.0f) {
                continue;
            }

            const float left = std::clamp(src.currentLeft * duckFactor, 0.0f, 1.0f);
            const float right = std::clamp(src.currentRight * duckFactor, 0.0f, 1.0f);
            const float score =
                std::max(left, right) * (0.40f + 0.60f * std::clamp(src.msg.confidence, 0.0f, 1.0f)) +
                (0.001f * src.msg.priority);

            if (IsBackgroundEvent(src.msg.eventType)) {
                ++backgroundIsolated;
            }
            else {
                const auto family = ClassifyForegroundFamily(src.msg.eventType);
                const auto idx = static_cast<std::size_t>(family);
                auto& agg = familyAgg[idx];
                if (!familyUsed[idx]) {
                    familyUsed[idx] = true;
                    agg.left = left;
                    agg.right = right;
                    agg.weight = weight;
                    agg.score = score;
                    agg.eventType = src.msg.eventType;
                    agg.sourceType = src.msg.type;
                    agg.confidence = std::clamp(src.msg.confidence, 0.0f, 1.0f);
                    agg.priority = static_cast<std::uint8_t>(std::clamp(src.msg.priority, 0, 255));
                    agg.sourceFormId = src.msg.sourceFormId;
                    agg.flags = src.msg.flags;
                }
                else {
                    agg.left = std::max(agg.left, left);
                    agg.right = std::max(agg.right, right);
                    agg.weight = std::max(agg.weight, weight);
                    if (score > agg.score) {
                        agg.score = score;
                        agg.eventType = src.msg.eventType;
                        agg.sourceType = src.msg.type;
                        agg.confidence = std::clamp(src.msg.confidence, 0.0f, 1.0f);
                        agg.priority = static_cast<std::uint8_t>(std::clamp(src.msg.priority, 0, 255));
                        agg.sourceFormId = src.msg.sourceFormId;
                        agg.flags = src.msg.flags;
                    }
                }
            }
        }

        for (std::size_t i = 0; i < familyUsed.size(); ++i) {
            if (familyUsed[i]) {
                foreground.push_back(familyAgg[i]);
            }
        }
        _foregroundFamilies.store(static_cast<std::uint32_t>(foreground.size()), std::memory_order_relaxed);

        const std::size_t topN = std::max<std::size_t>(1, config.mixerForegroundTopN);
        const std::size_t selectedFgCount = std::min(topN, foreground.size());
        _activeForeground.store(static_cast<std::uint32_t>(selectedFgCount), std::memory_order_relaxed);

        if (foreground.size() > selectedFgCount) {
            const auto dropped = static_cast<std::uint32_t>(foreground.size() - selectedFgCount);
            _budgetDropCount.fetch_add(
                dropped,
                std::memory_order_relaxed);
            _budgetDropForeground.fetch_add(
                dropped,
                std::memory_order_relaxed);
        }

        if (selectedFgCount > 0) {
            std::partial_sort(
                foreground.begin(),
                foreground.begin() + selectedFgCount,
                foreground.end(),
                [](const WeightedSource& a, const WeightedSource& b) { return a.score > b.score; });

            if (config.mixerSameGroupMode == HapticsConfig::MixerSameGroupMode::Max) {
                for (std::size_t i = 0; i < selectedFgCount; ++i) {
                    const auto& w = foreground[i];
                    outLeft = std::max(outLeft, w.left);
                    outRight = std::max(outRight, w.right);
                }
            }
            else {
                float totalWeight = 0.0f;
                for (std::size_t i = 0; i < selectedFgCount; ++i) {
                    const auto& w = foreground[i];
                    outLeft += w.left * w.weight;
                    outRight += w.right * w.weight;
                    totalWeight += w.weight;
                }

                if (totalWeight > 0.0f) {
                    outLeft /= totalWeight;
                    outRight /= totalWeight;
                }
            }

            if (outAttribution && selectedFgCount > 0) {
                const auto& dom = foreground.front();
                outAttribution->valid = true;
                outAttribution->eventType = dom.eventType;
                outAttribution->sourceType = dom.sourceType;
                outAttribution->confidence = dom.confidence;
                outAttribution->priority = dom.priority;
                outAttribution->sourceFormId = dom.sourceFormId;
                outAttribution->flags = dom.flags;
            }
        }

        if (backgroundIsolated > 0) {
            _activeBackground.store(backgroundIsolated, std::memory_order_relaxed);
            _budgetDropCount.fetch_add(backgroundIsolated, std::memory_order_relaxed);
            _budgetDropBackground.fetch_add(backgroundIsolated, std::memory_order_relaxed);
        }
        else {
            _activeBackground.store(0, std::memory_order_relaxed);
        }
    }

    void HapticMixer::ApplyDucking(float& left, float& right)
    {
        (void)left;
        (void)right;
    }

    void HapticMixer::ApplyCompressor(float& left, float& right)
    {
        constexpr float threshold = 0.7f;
        constexpr float ratio = 3.0f;

        auto compress = [](float x, float thresh, float r) -> float {
            if (x <= thresh) return x;
            float over = x - thresh;
            return thresh + over / r;
            };

        left = compress(left, threshold, ratio);
        right = compress(right, threshold, ratio);
    }

    void HapticMixer::ApplyLimiter(float& left, float& right)
    {
        auto& config = HapticsConfig::GetSingleton();
        left = std::clamp(left, 0.0f, config.limiter);
        right = std::clamp(right, 0.0f, config.limiter);
    }

    void HapticMixer::ApplySlewLimit(float& left, float& right)
    {
        auto& config = HapticsConfig::GetSingleton();

        if (left <= 0.0f && right <= 0.0f) {
            _lastLeft = 0.0f;
            _lastRight = 0.0f;
            return;
        }

        float maxDelta = config.slewPerTick / 255.0f;

        float deltaLeft = left - _lastLeft;
        float deltaRight = right - _lastRight;

        if (std::abs(deltaLeft) > maxDelta) {
            left = _lastLeft + (deltaLeft > 0 ? maxDelta : -maxDelta);
        }

        if (std::abs(deltaRight) > maxDelta) {
            right = _lastRight + (deltaRight > 0 ? maxDelta : -maxDelta);
        }

        _lastLeft = left;
        _lastRight = right;
    }

    void HapticMixer::ApplyDeadzone(float& left, float& right)
    {
        auto& config = HapticsConfig::GetSingleton();

        if (left < config.deadzone) left = 0.0f;
        if (right < config.deadzone) right = 0.0f;
    }

    void HapticMixer::RefreshEventLease(
        const FrameAttribution& attribution,
        float left,
        float right,
        std::uint64_t nowUs)
    {
        if (!attribution.valid) {
            return;
        }

        const float targetLeft = std::clamp(left, 0.0f, 1.0f);
        const float targetRight = std::clamp(right, 0.0f, 1.0f);
        const float targetPeak = std::max(targetLeft, targetRight);
        if (targetPeak < kEventLeaseSignalFloor) {
            return;
        }

        const bool structured =
            attribution.sourceFormId != 0 ||
            (attribution.flags & HapticSourceFlagL1Trace) != 0 ||
            (attribution.flags & HapticSourceFlagSessionPromoted) != 0;
        if (attribution.eventType == EventType::Unknown && !structured) {
            return;
        }

        if (attribution.eventType != EventType::Unknown &&
            IsBackgroundEvent(attribution.eventType)) {
            return;
        }

        const auto holdMs = GetEventLeaseHoldMs(attribution.eventType, structured);
        const auto releaseMs = GetEventLeaseReleaseMs(attribution.eventType, structured);
        if (holdMs == 0u || releaseMs == 0u) {
            return;
        }

        const float incomingConfidence = std::clamp(attribution.confidence, 0.0f, 1.0f);
        const bool replaceExpired = !_eventLease.active || nowUs >= _eventLease.releaseEndUs;
        const bool sameForm =
            _eventLease.active &&
            attribution.sourceFormId != 0 &&
            attribution.sourceFormId == _eventLease.sourceFormId;
        const bool sameEvent =
            _eventLease.active &&
            attribution.eventType != EventType::Unknown &&
            attribution.eventType == _eventLease.eventType;
        const bool stronger =
            !_eventLease.active ||
            attribution.priority > _eventLease.priority ||
            incomingConfidence >= (_eventLease.confidence + 0.08f) ||
            targetPeak >= (std::max(_eventLease.targetLeft, _eventLease.targetRight) * 0.92f);

        if (replaceExpired || sameForm || sameEvent || stronger) {
            if (replaceExpired || !sameForm) {
                _eventLease.valueLeft = std::max(_eventLease.valueLeft, targetLeft * 0.35f);
                _eventLease.valueRight = std::max(_eventLease.valueRight, targetRight * 0.35f);
            }
            _eventLease.active = true;
            _eventLease.eventType = attribution.eventType;
            _eventLease.sourceType = attribution.sourceType;
            _eventLease.confidence = incomingConfidence;
            _eventLease.priority = attribution.priority;
            _eventLease.sourceFormId = attribution.sourceFormId;
            _eventLease.flags = attribution.flags;

            if (replaceExpired || !sameEvent) {
                _eventLease.targetLeft = targetLeft;
                _eventLease.targetRight = targetRight;
            }
            else {
                _eventLease.targetLeft = std::clamp(
                    _eventLease.targetLeft * 0.40f + targetLeft * 0.60f,
                    0.0f,
                    1.0f);
                _eventLease.targetRight = std::clamp(
                    _eventLease.targetRight * 0.40f + targetRight * 0.60f,
                    0.0f,
                    1.0f);
            }

            const auto holdUntilUs = nowUs + static_cast<std::uint64_t>(holdMs) * 1000ull;
            _eventLease.holdUntilUs = std::max(_eventLease.holdUntilUs, holdUntilUs);
            const auto releaseEndUs = _eventLease.holdUntilUs +
                static_cast<std::uint64_t>(releaseMs) * 1000ull;
            _eventLease.releaseEndUs = std::max(_eventLease.releaseEndUs, releaseEndUs);
        }
    }

    void HapticMixer::ApplyEventLease(
        float& left,
        float& right,
        FrameAttribution& attribution,
        std::uint64_t nowUs)
    {
        if (!_eventLease.active) {
            return;
        }

        if (nowUs >= _eventLease.releaseEndUs) {
            _eventLease = {};
            return;
        }

        const auto holdUntilUs = _eventLease.holdUntilUs;
        const auto releaseEndUs = std::max(holdUntilUs + 1000ull, _eventLease.releaseEndUs);
        const bool inHold = nowUs <= holdUntilUs;

        float envelope = 1.0f;
        if (!inHold) {
            envelope = 1.0f -
                static_cast<float>(nowUs - holdUntilUs) /
                static_cast<float>(releaseEndUs - holdUntilUs);
            envelope = std::clamp(envelope, 0.0f, 1.0f);
        }

        const float alpha = inHold ? 0.64f : 0.30f;
        _eventLease.valueLeft = std::clamp(
            _eventLease.valueLeft + (_eventLease.targetLeft - _eventLease.valueLeft) * alpha,
            0.0f,
            1.0f);
        _eventLease.valueRight = std::clamp(
            _eventLease.valueRight + (_eventLease.targetRight - _eventLease.valueRight) * alpha,
            0.0f,
            1.0f);

        const float leaseLeft = _eventLease.valueLeft * envelope;
        const float leaseRight = _eventLease.valueRight * envelope;
        if (leaseLeft > left) {
            left = leaseLeft;
        }
        if (leaseRight > right) {
            right = leaseRight;
        }

        const bool leaseNonZero = (leaseLeft > kNonZeroSignalFloor || leaseRight > kNonZeroSignalFloor);
        if (leaseNonZero && (!attribution.valid || attribution.eventType == EventType::Unknown)) {
            attribution.valid = true;
            attribution.eventType = _eventLease.eventType;
            attribution.sourceType = _eventLease.sourceType;
            attribution.confidence = _eventLease.confidence;
            attribution.priority = _eventLease.priority;
            attribution.sourceFormId = _eventLease.sourceFormId;
            attribution.flags = _eventLease.flags;
        }

        if (!inHold &&
            !leaseNonZero &&
            nowUs + 1000ull >= releaseEndUs) {
            _eventLease = {};
        }
    }

    float HapticMixer::GetDuckingFactor(SourceType type) const
    {
        (void)type;
        return 1.0f;
    }

    bool HapticMixer::HasHighPrioritySource() const
    {
        auto& config = HapticsConfig::GetSingleton();

        std::scoped_lock lock(_mutex);
        for (const auto& src : _activeSources) {
            if (src.msg.priority >= config.priorityHit) {
                return true;
            }
        }
        return false;
    }

    EventType HapticMixer::GetEventTypeFromSource(const ActiveSource& src) const
    {
        return src.msg.eventType;
    }
}
