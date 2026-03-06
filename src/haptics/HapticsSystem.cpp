#include "pch.h"
#include "haptics/HapticsSystem.h"

#include "haptics/HapticsConfig.h"
#include "haptics/VoiceManager.h"
#include "haptics/HapticMixer.h"
#include "haptics/HidOutput.h"
#include "haptics/DecisionEngine.h"
#include "haptics/DynamicHapticPool.h"
#include "haptics/EngineAudioTap.h"
#include "haptics/EventNormalizer.h"
#include "haptics/FootstepAudioMatcher.h"
#include "haptics/FootstepTruthBridge.h"
#include "haptics/FootstepTruthProbe.h"
#include "haptics/HapticEligibilityEngine.h"
#include "haptics/MetricsReporter.h"
#include "haptics/PlayPathHook.h"
#include "haptics/AudioOnlyScorer.h"
#include "haptics/FormSemanticCache.h"
#include "haptics/SemanticResolver.h"
#include "haptics/SessionFormPromoter.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        float PercentOf(std::uint64_t part, std::uint64_t total)
        {
            if (total == 0) {
                return 0.0f;
            }
            return (100.0f * static_cast<float>(part)) / static_cast<float>(total);
        }

        const char* ToString(SemanticGroup group)
        {
            switch (group) {
            case SemanticGroup::WeaponSwing: return "WeaponSwing";
            case SemanticGroup::Hit:         return "Hit";
            case SemanticGroup::Block:       return "Block";
            case SemanticGroup::Footstep:    return "Footstep";
            case SemanticGroup::Bow:         return "Bow";
            case SemanticGroup::Voice:       return "Voice";
            case SemanticGroup::UI:          return "UI";
            case SemanticGroup::Music:       return "Music";
            case SemanticGroup::Ambient:     return "Ambient";
            default:                         return "Unknown";
            }
        }

        std::string BuildUnknownTopSummary(const EventNormalizer::Stats& s)
        {
            if (s.unknownTopCount == 0) {
                return "none";
            }

            std::ostringstream oss;
            for (std::uint32_t i = 0; i < s.unknownTopCount; ++i) {
                if (i > 0) {
                    oss << ", ";
                }

                const auto& item = s.unknownTop[i];
                oss << "0x"
                    << std::hex << std::uppercase << item.formID
                    << std::dec
                    << ":" << item.hits
                    << ":" << ToString(item.semantic)
                    << "(" << item.semanticConfidence << ")";
            }
            return oss.str();
        }

        const char* ToString(RE::FormType formType)
        {
            switch (formType) {
            case RE::FormType::Sound:         return "Sound";
            case RE::FormType::SoundRecord:   return "SoundRecord";
            case RE::FormType::SoundCategory: return "SoundCategory";
            case RE::FormType::MusicType:     return "MusicType";
            case RE::FormType::MusicTrack:    return "MusicTrack";
            case RE::FormType::Footstep:      return "Footstep";
            case RE::FormType::FootstepSet:   return "FootstepSet";
            case RE::FormType::Impact:        return "Impact";
            case RE::FormType::ImpactDataSet: return "ImpactDataSet";
            case RE::FormType::AcousticSpace: return "AcousticSpace";
            default:                          return "Other";
            }
        }

        std::string TrimEditorID(const char* editorID)
        {
            if (!editorID || editorID[0] == '\0') {
                return "<none>";
            }

            std::string out(editorID);
            constexpr std::size_t kMaxEdidLen = 48;
            if (out.size() > kMaxEdidLen) {
                out.resize(kMaxEdidLen);
                out += "...";
            }
            return out;
        }

        std::string BuildUnknownTopResolvedSummary(const EventNormalizer::Stats& s)
        {
            if (s.unknownTopCount == 0) {
                return "none";
            }

            // During shutdown TES data can already be torn down; avoid form lookups then.
            if (!RE::TESDataHandler::GetSingleton()) {
                return "tesdata=unavailable";
            }

            std::ostringstream oss;
            for (std::uint32_t i = 0; i < s.unknownTopCount; ++i) {
                if (i > 0) {
                    oss << ", ";
                }

                const auto& item = s.unknownTop[i];
                oss << "0x"
                    << std::hex << std::uppercase << item.formID
                    << std::dec
                    << ":" << item.hits
                    << ":" << ToString(item.semantic)
                    << "(" << item.semanticConfidence << ")";

                auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(item.formID));
                if (!form) {
                    oss << "/missing";
                    continue;
                }

                oss << "/type=" << ToString(form->GetFormType())
                    << "/edid=" << TrimEditorID(form->GetFormEditorID());
            }

            return oss.str();
        }

        void ExportUnknownOverrideTemplate(const EventNormalizer::Stats& s)
        {
            if (s.unknownTopCount == 0) {
                return;
            }

            const std::filesystem::path path = "Data/SKSE/Plugins/DualPadSemanticOverrides.template.json";
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                logger::warn(
                    "[Haptics][SessionSummary] create override template dir failed: {} ({})",
                    path.parent_path().string(),
                    ec.message());
                return;
            }

            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                logger::warn("[Haptics][SessionSummary] open override template failed: {}", path.string());
                return;
            }

            ofs << "{\n";
            ofs << "  \"version\": 1,\n";
            ofs << "  \"hardOverrides\": {\n";

            std::size_t written = 0;
            for (std::uint32_t i = 0; i < s.unknownTopCount; ++i) {
                const auto& item = s.unknownTop[i];
                if (item.formID == 0) {
                    continue;
                }

                std::ostringstream formID;
                formID << "0x" << std::uppercase << std::hex << std::setw(8)
                    << std::setfill('0') << item.formID;

                if (written > 0) {
                    ofs << ",\n";
                }

                ofs << "    \"" << formID.str() << "\": {\"group\": \"Unknown\", \"confidence\": 0.60, \"baseWeight\": 0.50, \"note\": \"unknownTop hits="
                    << item.hits << "; fill real group manually\"}";
                ++written;
            }

            ofs << "\n  }\n";
            ofs << "}\n";

            if (!ofs.good()) {
                logger::warn("[Haptics][SessionSummary] write override template failed: {}", path.string());
                return;
            }

            logger::info(
                "[Haptics][SessionSummary] wrote unknown override template path={} entries={}",
                path.string(),
                written);
        }

        void LogSystemDivider()
        {
            logger::info("[Haptics][System] ========================================");
        }

        void LogSystemBanner(const char* text)
        {
            LogSystemDivider();
            logger::info("[Haptics][System] {}", text);
            LogSystemDivider();
        }

        void LogPlayPathStats(const PlayPathHook::Stats& playStats)
        {
            logger::info(
                "[Haptics][PlayPath] initCalls={} initResolved={} submitCalls={} submitResolved={} noContext={} noForm={} noForm1={} noFormR={} scan={} retryRes={} noCtxScan={} noCtxRes={} noCtxNoPtr={} noCtxDeepScan={} noCtxDeepRes={} skipResolved={} skipInit={} skipSub={} skipOth={} retry={} skipRL={} skipMax={} traceHit={} traceMiss={} bindMiss={} upserts={}",
                playStats.initCalls,
                playStats.initResolved,
                playStats.submitCalls,
                playStats.submitResolved,
                playStats.submitNoContext,
                playStats.submitNoForm,
                playStats.submitNoFormFirstScan,
                playStats.submitNoFormRetry,
                playStats.submitScanExecuted,
                playStats.submitResolvedOnRetry,
                playStats.submitNoContextScan,
                playStats.submitNoContextResolved,
                playStats.submitNoContextNoInitPtr,
                playStats.submitNoContextDeepScan,
                playStats.submitNoContextDeepResolved,
                playStats.submitSkipResolved,
                playStats.submitSkipResolvedFromInit,
                playStats.submitSkipResolvedFromSubmit,
                playStats.submitSkipResolvedOther,
                playStats.submitRetryScans,
                playStats.submitSkipRateLimit,
                playStats.submitSkipMaxAttempts,
                playStats.submitTraceMetaHit,
                playStats.submitTraceMetaMiss,
                playStats.bindingMisses,
                playStats.traceUpserts);
        }

        void LogSessionPlayPathSummary(
            const PlayPathHook::Stats& play,
            const EventNormalizer::Stats& norm)
        {
            logger::info(
                "[Haptics][SessionSummary] playPath initResolved={}/{} ({:.1f}%) submitResolved={}/{} ({:.1f}%) noCtx={} noForm={} (first={} retry={}) scan={} retryRes={} noCtxScan={} noCtxRes={} noCtxNoPtr={} noCtxDeepScan={} noCtxDeepRes={} skipResolved={} (init={} submit={} other={}) retry={} skipRL={} skipMax={} traceMeta(hit={} miss={}) patchForm={} patchEvent={} patchEventCons={} traceMiss={} bindMiss={}",
                play.initResolved,
                play.initCalls,
                PercentOf(play.initResolved, play.initCalls),
                play.submitResolved,
                play.submitCalls,
                PercentOf(play.submitResolved, play.submitCalls),
                play.submitNoContext,
                play.submitNoForm,
                play.submitNoFormFirstScan,
                play.submitNoFormRetry,
                play.submitScanExecuted,
                play.submitResolvedOnRetry,
                play.submitNoContextScan,
                play.submitNoContextResolved,
                play.submitNoContextNoInitPtr,
                play.submitNoContextDeepScan,
                play.submitNoContextDeepResolved,
                play.submitSkipResolved,
                play.submitSkipResolvedFromInit,
                play.submitSkipResolvedFromSubmit,
                play.submitSkipResolvedOther,
                play.submitRetryScans,
                play.submitSkipRateLimit,
                play.submitSkipMaxAttempts,
                play.submitTraceMetaHit,
                play.submitTraceMetaMiss,
                norm.patchedFormID,
                norm.patchedEventType,
                norm.patchedEventTypeConservative,
                norm.traceMiss,
                norm.bindingMiss);
        }
    }

    HapticsSystem& HapticsSystem::GetSingleton()
    {
        static HapticsSystem instance;
        return instance;
    }

    bool HapticsSystem::Initialize()
    {
        if (_initialized) {
            logger::warn("[Haptics][System] Already initialized");
            return true;
        }

        LogSystemBanner("Initializing Haptics System...");

        if (!InitializeConfig()) {
            logger::error("[Haptics][System] Failed to initialize config");
            return false;
        }

        auto& config = HapticsConfig::GetSingleton();
        if (config.enableFormSemanticCache) {
            if (!FormSemanticCache::GetSingleton().Initialize()) {
                logger::warn("[Haptics][System] FormSemanticCache init failed, continue fail-open");
            }
        }

        if (config.IsNativeOnly()) {
            _corePipelineInitialized.store(false, std::memory_order_release);
            _initialized = true;

            logger::info("[Haptics][System] NativeOnly: skip custom core pipeline initialization");
            LogSystemBanner("Haptics System Initialized (NativeOnly)");
            return true;
        }

        if (!InitializeCorePipeline()) {
            logger::error("[Haptics][System] Failed to initialize core pipeline");
            return false;
        }

        _corePipelineInitialized.store(true, std::memory_order_release);
        _initialized = true;

        LogSystemBanner("Haptics System Initialized (CustomAudio)");
        return true;
    }

    bool HapticsSystem::Start()
    {
        if (!_initialized) {
            logger::error("[Haptics][System] Cannot start: not initialized");
            return false;
        }

        if (_running.exchange(true)) {
            logger::warn("[Haptics][System] Already running");
            return true;
        }

        LogSystemBanner("Starting Haptics System...");

        auto& config = HapticsConfig::GetSingleton();
        if (config.IsNativeOnly()) {
            _customPipelineActive.store(false, std::memory_order_release);

            logger::info("[Haptics][System] Mode=NativeOnly");
            logger::info("[Haptics][System] Custom pipeline disabled: no tap / no mixer / no custom HID output");
            LogSystemBanner("Haptics System Started (NativeOnly Mode)");
            return true;
        }

        if (!_corePipelineInitialized.load(std::memory_order_acquire)) {
            logger::error("[Haptics][System] core pipeline not initialized in CustomAudio mode");
            _running.store(false, std::memory_order_release);
            return false;
        }

        if (!EngineAudioTap::Install()) {
            logger::error("[Haptics][System] EngineAudioTap install failed");
            _running.store(false, std::memory_order_release);
            _customPipelineActive.store(false, std::memory_order_release);
            return false;
        }

        if (!InitializeThreads()) {
            logger::error("[Haptics][System] Failed to start threads");
            EngineAudioTap::Uninstall();
            _running.store(false, std::memory_order_release);
            _customPipelineActive.store(false, std::memory_order_release);
            return false;
        }

        if (!FootstepTruthProbe::GetSingleton().Register()) {
            logger::warn("[Haptics][System] FootstepTruthProbe register failed, continue fail-open");
        }
        FootstepTruthBridge::GetSingleton().Reset();
        FootstepAudioMatcher::GetSingleton().Reset();

        _customPipelineActive.store(true, std::memory_order_release);

        LogSystemDivider();
        logger::info("[Haptics][System] Haptics System Started (CustomAudio)");
        PrintStats();
        LogSystemDivider();
        return true;
    }

    void HapticsSystem::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        LogSystemBanner("Stopping Haptics System...");

        const bool customActive = _customPipelineActive.exchange(false, std::memory_order_acq_rel);
        if (customActive) {
            FootstepTruthProbe::GetSingleton().Unregister();
            StopThreads();
            PrintSessionSummary();
            HidOutput::GetSingleton().StopVibration();
            EngineAudioTap::Uninstall();
        }

        LogSystemBanner("Haptics System Stopped");
    }

    void HapticsSystem::Shutdown()
    {
        if (!_initialized) {
            return;
        }

        LogSystemBanner("Shutting down Haptics System...");

        Stop();

        if (_corePipelineInitialized.exchange(false, std::memory_order_acq_rel)) {
            ShutdownCorePipeline();
        }

        _initialized = false;

        LogSystemBanner("Haptics System Shutdown Complete");
    }

    void HapticsSystem::PrintStats()
    {
        if (!_initialized) {
            logger::info("[Haptics][System] Not initialized");
            return;
        }

        if (!_corePipelineInitialized.load(std::memory_order_acquire)) {
            logger::info("[Haptics][System] NativeOnly stats: custom pipeline not initialized");
            return;
        }

        LogSystemBanner("Haptics System Statistics (AudioOnly)");

        auto tapStats = EngineAudioTap::GetStats();
        logger::info("[Haptics][SubmitTap] calls={} feature_pushed={} compressed_skip={}",
            tapStats.submitCalls,
            tapStats.submitFeaturesPushed,
            tapStats.submitCompressedSkipped);

        auto voiceStats = VoiceManager::GetSingleton().GetStats();
        logger::info("[Haptics][VoiceManager] pushed={} dropped={}",
            voiceStats.featuresPushed, voiceStats.featuresDropped);

        auto playStats = PlayPathHook::GetSingleton().GetStats();
        LogPlayPathStats(playStats);

        auto audioStats = AudioOnlyScorer::GetSingleton().GetStats();
        logger::info("[Haptics][AudioOnlyScorer] pulled={} produced={} lowEnergyDropped={} relativeEnergyDropped={}",
            audioStats.featuresPulled,
            audioStats.sourcesProduced,
            audioStats.lowEnergyDropped,
            audioStats.relativeEnergyDropped);

        auto normStats = EventNormalizer::GetSingleton().GetStats();
        logger::info("[Haptics][EventNormalizer] inputs={} noVoice={} bindMiss={} traceMiss={} traceHit={} patchForm={} patchEvent={} patchEventCons={} unknownAfter={} unknownForm={} unknownNoForm={} unknownMapOv={}",
            normStats.inputs,
            normStats.noVoiceID,
            normStats.bindingMiss,
            normStats.traceMiss,
            normStats.traceHit,
            normStats.patchedFormID,
            normStats.patchedEventType,
            normStats.patchedEventTypeConservative,
            normStats.unknownAfterNormalize,
            normStats.unknownWithFormID,
            normStats.unknownNoFormID,
            normStats.unknownMapOverflow);
        if (normStats.unknownAfterNormalize > 0) {
            logger::info(
                "[Haptics][UnknownTop] after={} withForm={} noForm={} mapOv={} top={}",
                normStats.unknownAfterNormalize,
                normStats.unknownWithFormID,
                normStats.unknownNoFormID,
                normStats.unknownMapOverflow,
                BuildUnknownTopSummary(normStats));
            logger::info(
                "[Haptics][UnknownTopResolved] {}",
                BuildUnknownTopResolvedSummary(normStats));
        }

        auto resolverStats = SemanticResolver::GetSingleton().GetStats();
        logger::info("[Haptics][SemanticResolver] lookups={} hits={} noForm={} cacheMiss={} lowConfidence={}",
            resolverStats.lookups,
            resolverStats.hits,
            resolverStats.noFormID,
            resolverStats.cacheMiss,
            resolverStats.lowConfidence);

        auto metricSnap = MetricsReporter::GetSingleton().SnapshotAndReset(
            VoiceManager::GetSingleton().GetQueueSize(),
            voiceStats.featuresDropped);
        logger::info(
            "[Haptics][Metrics] latP50={}us latP95={}us samples={} unknown={:.2f} metaMis={:.2f} qDepth={} drop={}",
            metricSnap.latencyP50Us,
            metricSnap.latencyP95Us,
            metricSnap.sampleCount,
            metricSnap.unknownRatio,
            metricSnap.metaMismatchRatio,
            metricSnap.queueDepth,
            metricSnap.dropCount);

        auto decStats = DecisionEngine::GetSingleton().GetStats();
        auto eligibilityStats = HapticEligibilityEngine::GetSingleton().GetStats();
        auto dynamicStats = DynamicHapticPool::GetSingleton().GetStats();
        logger::info(
            "[Haptics][DynamicPool] size={} observe={} admitted={} rejNoKey={} rejLow={} shCall={} shHit={} shMiss={} hit={} miss={} rejMinHit={} rejLowIn={} evicted={} learnL2={} learnNoKey={} learnLowScore={}",
            dynamicStats.currentSize,
            dynamicStats.observeCalls,
            dynamicStats.admitted,
            dynamicStats.rejectedNoKey,
            dynamicStats.rejectedLowConfidence,
            dynamicStats.shadowCalls,
            dynamicStats.shadowHits,
            dynamicStats.shadowMisses,
            dynamicStats.resolveHits,
            dynamicStats.resolveMisses,
            dynamicStats.resolveRejectMinHits,
            dynamicStats.resolveRejectLowInput,
            dynamicStats.evicted,
            decStats.dynamicPoolLearnFromL2,
            decStats.dynamicPoolLearnFromL2NoKey,
            decStats.dynamicPoolLearnFromL2LowScore);

        auto semanticStats = FormSemanticCache::GetSingleton().GetStats();
        logger::info("[Haptics][FormSemantic] entries={} hits={} misses={} loads={} rebuilds={}",
            FormSemanticCache::GetSingleton().Size(),
            semanticStats.hits,
            semanticStats.misses,
            semanticStats.loads,
            semanticStats.rebuilds);

        auto mixerStats = HapticMixer::GetSingleton().GetStats();
        logger::info("[Haptics][Mixer] Active={} Frames={} Ticks={} Sources={} ClampUnk={} ClampBg={} BgIso={} FgFam={} DropEvt={} DropUnkLow={} DropUnkSem={} Meta(active/carrier)={}/{} DropOutUnk={} PeakL={:.3f} PeakR={:.3f}",
            mixerStats.activeSources,
            mixerStats.framesOutput,
            mixerStats.totalTicks,
            mixerStats.totalSourcesAdded,
            mixerStats.softClampUnknown,
            mixerStats.softClampBackground,
            mixerStats.dropBackgroundHardIsolated,
            mixerStats.foregroundFamilies,
            mixerStats.dropEventDisabled,
            mixerStats.dropUnknownLowInput,
            mixerStats.dropUnknownSemantic,
            mixerStats.outputMetaFromActive,
            mixerStats.outputMetaFromCarrier,
            mixerStats.outputDropUnknownNonZero,
            mixerStats.peakLeft,
            mixerStats.peakRight);
        logger::info(
            "[Haptics][ProbeGate] rej={} unk={} bg={} noTrace={} lowSem={} lowRel={} refrHard={} refrWindow={} refrSoft={}",
            decStats.gateRejected,
            decStats.rejectUnknownBlocked,
            decStats.rejectBackgroundBlocked,
            decStats.rejectNoTraceContext,
            decStats.rejectLowSemanticConfidence,
            decStats.rejectLowRelativeEnergy,
            eligibilityStats.refractoryHardDropped,
            eligibilityStats.refractoryWindowHit,
            eligibilityStats.refractorySoftSuppressed);
        logger::info(
            "[Haptics][ProbeMixer] add(call={} ins={} mergeForm={} mergeLock={} dropUnkBudget={} lateRescue={} bgIso={}) age(sample={} <8ms={} 8-20={} 20-50={} 50-100={} 100+={} max={}us) budget(unk={} fg={} bg={} total={} fgFamilies={})",
            mixerStats.sourceAddCalls,
            mixerStats.sourceInserted,
            mixerStats.sourceMergedSameForm,
            mixerStats.sourceMergedAudioLock,
            mixerStats.sourceDropUnknownBudget,
            mixerStats.sourceLateRescue,
            mixerStats.dropBackgroundHardIsolated,
            mixerStats.sourceAgeSamples,
            mixerStats.sourceAgeLt8Ms,
            mixerStats.sourceAge8To20Ms,
            mixerStats.sourceAge20To50Ms,
            mixerStats.sourceAge50To100Ms,
            mixerStats.sourceAge100MsPlus,
            mixerStats.sourceAgeMaxUs,
            mixerStats.budgetDropUnknown,
            mixerStats.budgetDropForeground,
            mixerStats.budgetDropBackground,
            mixerStats.budgetDropCount,
            mixerStats.foregroundFamilies);

        auto hidStats = HidOutput::GetSingleton().GetStats();
        auto footTruth = FootstepTruthProbe::GetSingleton().GetStats();
        auto footBridge = FootstepTruthBridge::GetSingleton().GetStats();
        auto footAudio = FootstepAudioMatcher::GetSingleton().GetStats();
        logger::info(
            "[Haptics][HidOutput] Frames={} Bytes={} Failures={} sendOk={} sendFail={} noDev={} writeFail={} qDepth(FG/BG)={}/{}",
            hidStats.totalFramesSent,
            hidStats.totalBytesSent,
            hidStats.sendFailures,
            hidStats.txSendOk,
            hidStats.txSendFail,
            hidStats.txNoDevice,
            hidStats.sendWriteFail,
            hidStats.txQueueDepthFg,
            hidStats.txQueueDepthBg);
        logger::info(
            "[Haptics][TxStats] q(FG/BG)={}/{} dq(FG/BG)={}/{} dropFull(FG/BG)={}/{} dropStale(FG/BG)={}/{} merge(FG/BG)={}/{} renderP50={}us renderP95={}us renderN={} firstP50={}us firstP95={}us firstN={} skipRpt={} stopFlush={} route(fg/bg={}/{}, fgZero/hint/prio/evt={}/{}/{}/{}, bgUnk/bgEvt={}/{}) select(forceFg/bgWhileFg={}/{})",
            hidStats.txQueuedFg,
            hidStats.txQueuedBg,
            hidStats.txDequeuedFg,
            hidStats.txDequeuedBg,
            hidStats.txDropQueueFullFg,
            hidStats.txDropQueueFullBg,
            hidStats.txDropStaleFg,
            hidStats.txDropStaleBg,
            hidStats.txMergedFg,
            hidStats.txMergedBg,
            hidStats.txRenderOverP50Us,
            hidStats.txRenderOverP95Us,
            hidStats.txRenderOverSamples,
            hidStats.txFirstRenderP50Us,
            hidStats.txFirstRenderP95Us,
            hidStats.txFirstRenderSamples,
            hidStats.txSkippedRepeat,
            hidStats.txStopFlushes,
            hidStats.txRouteFg,
            hidStats.txRouteBg,
            hidStats.txRouteFgZero,
            hidStats.txRouteFgHint,
            hidStats.txRouteFgPriority,
            hidStats.txRouteFgEvent,
            hidStats.txRouteBgUnknown,
            hidStats.txRouteBgBackground,
            hidStats.txSelectForcedFgBudget,
            hidStats.txSelectBgWhileFgPending);
        logger::info(
            "[Haptics][FootTruth] total={} player={} admissible={} shadow(match={} truthMiss={} renderMiss={} deltaP50={}us deltaP95={}us samples={} pending={})",
            footTruth.totalEvents,
            footTruth.playerEvents,
            footTruth.admissibleEvents,
            footTruth.shadowMatchedRenders,
            footTruth.shadowExpiredTruthMisses,
            footTruth.shadowRenderWithoutTruth,
            footTruth.shadowRenderDeltaP50Us,
            footTruth.shadowRenderDeltaP95Us,
            footTruth.shadowRenderDeltaSamples,
            footTruth.shadowPendingTruth);
        logger::info(
            "[Haptics][FootBridge] truths={} inst={} claims={} init={} submit={} miss(truth/inst)={}/{} deltaP50={}us deltaP95={}us samples={} pending(t/i/b)={}/{}/{}",
            footBridge.truthsObserved,
            footBridge.instancesObserved,
            footBridge.claimsMatched,
            footBridge.claimsFromInit,
            footBridge.claimsFromSubmit,
            footBridge.truthExpiredMisses,
            footBridge.instanceExpiredMisses,
            footBridge.deltaP50Us,
            footBridge.deltaP95Us,
            footBridge.deltaSamples,
            footBridge.pendingTruths,
            footBridge.pendingInstances,
            footBridge.activeBindings);
        logger::info(
            "[Haptics][FootAudio] features={} truths={} matched={} bridge(bound/match/noFeat)={}/{}/{} noWin={} noSem={} lowScore={} cand(window/sem)={}/{} miss(bind/trace)={}/{} deltaP50={}us deltaP95={}us scoreP50={:.2f} scoreP95={:.2f} durP50={}us durP95={}us panAbsP50={:.2f} panAbsP95={:.2f} pending={}",
            footAudio.featuresObserved,
            footAudio.truthsObserved,
            footAudio.truthsMatched,
            footAudio.truthBridgeBound,
            footAudio.truthBridgeMatched,
            footAudio.truthBridgeNoFeature,
            footAudio.truthNoWindow,
            footAudio.truthNoSemantic,
            footAudio.truthLowScore,
            footAudio.windowCandidates,
            footAudio.semanticCandidates,
            footAudio.bindingMissCandidates,
            footAudio.traceMissCandidates,
            footAudio.matchDeltaP50Us,
            footAudio.matchDeltaP95Us,
            static_cast<float>(footAudio.matchScoreP50Permille) / 1000.0f,
            static_cast<float>(footAudio.matchScoreP95Permille) / 1000.0f,
            footAudio.matchDurationP50Us,
            footAudio.matchDurationP95Us,
            static_cast<float>(footAudio.matchPanAbsP50Permille) / 1000.0f,
            static_cast<float>(footAudio.matchPanAbsP95Permille) / 1000.0f,
            footAudio.pendingTruths);

        LogSystemDivider();
    }

    void HapticsSystem::PrintSessionSummary()
    {
        auto tap = EngineAudioTap::GetStats();
        auto dec = DecisionEngine::GetSingleton().GetStats();
        auto play = PlayPathHook::GetSingleton().GetStats();
        auto norm = EventNormalizer::GetSingleton().GetStats();
        auto dyn = DynamicHapticPool::GetSingleton().GetStats();
        auto sessionPromoter = SessionFormPromoter::GetSingleton().GetStats();
        auto voice = VoiceManager::GetSingleton().GetStats();
        auto hid = HidOutput::GetSingleton().GetStats();
        auto mixer = HapticMixer::GetSingleton().GetStats();
        auto eligibility = HapticEligibilityEngine::GetSingleton().GetStats();
        auto footTruth = FootstepTruthProbe::GetSingleton().GetStats();
        auto footBridge = FootstepTruthBridge::GetSingleton().GetStats();
        auto footAudio = FootstepAudioMatcher::GetSingleton().GetStats();

        const std::uint64_t totalDecisions = dec.l1Count + dec.l2Count + dec.l3Count;
        const std::uint64_t traceMissTotal =
            dec.traceBindMissUnbound + dec.traceBindMissExpired + dec.traceBindBypassDisabled;
        const std::uint64_t semanticRejectTotal =
            dec.l1FormSemanticNoFormID + dec.l1FormSemanticCacheMiss + dec.l1FormSemanticLowConfidence;

        logger::info("[Haptics][SessionSummary] ========================================");
        logger::info(
            "[Haptics][SessionSummary] decisions={} l1={}({:.1f}%) l2={}({:.1f}%) l3={}({:.1f}%)",
            totalDecisions,
            dec.l1Count, PercentOf(dec.l1Count, totalDecisions),
            dec.l2Count, PercentOf(dec.l2Count, totalDecisions),
            dec.l3Count, PercentOf(dec.l3Count, totalDecisions));

        logger::info(
            "[Haptics][SessionSummary] reason l2High={} l2Mid={} l2LowPass={} l1TraceHit={} l1SemanticHit={}",
            dec.l2HighScore,
            dec.l2MidScore,
            dec.l2LowScorePass,
            dec.traceBindHit,
            dec.l1FormSemanticHit);

        logger::info(
            "[Haptics][SessionSummary] noHit tickNoAudio={} audioNoMatch={} traceMiss={} (unbound={} expired={} disabled={})",
            dec.tickNoAudio,
            dec.audioPresentNoMatch,
            traceMissTotal,
            dec.traceBindMissUnbound,
            dec.traceBindMissExpired,
            dec.traceBindBypassDisabled);

        logger::info(
            "[Haptics][SessionSummary] reject norm(noVoice={} bindMiss={} traceMiss={}) sem(noForm={} cacheMiss={} lowConf={})",
            norm.noVoiceID,
            norm.bindingMiss,
            norm.traceMiss,
            dec.l1FormSemanticNoFormID,
            dec.l1FormSemanticCacheMiss,
            dec.l1FormSemanticLowConfidence);

        logger::info(
            "[Haptics][SessionSummary] gate acc={} rej={} unknown={} background={} noTrace={} lowSem={} lowRel={} refr={}",
            dec.gateAccepted,
            dec.gateRejected,
            dec.rejectUnknownBlocked,
            dec.rejectBackgroundBlocked,
            dec.rejectNoTraceContext,
            dec.rejectLowSemanticConfidence,
            dec.rejectLowRelativeEnergy,
            dec.rejectRefractoryBlocked);
        logger::info(
            "[Haptics][SessionSummary] gateRefr window={} soft={} hard={}",
            eligibility.refractoryWindowHit,
            eligibility.refractorySoftSuppressed,
            eligibility.refractoryHardDropped);

        logger::info(
            "[Haptics][SessionSummary] semantic miss={} (noForm={} cacheMiss={} lowConf={})",
            dec.l1FormSemanticMiss,
            dec.l1FormSemanticNoFormID,
            dec.l1FormSemanticCacheMiss,
            dec.l1FormSemanticLowConfidence);

        logger::info(
            "[Haptics][SessionSummary] fallback noAudio={} lowScore={} dynHit={} dynMiss={} l3DropWeakUnk={}",
            dec.tickNoAudio,
            dec.lowScoreFallback,
            dec.dynamicPoolHit,
            dec.dynamicPoolMiss,
            dec.l3DroppedUnstructuredWeakUnknown);

        LogSessionPlayPathSummary(play, norm);
        logger::info(
            "[Haptics][SessionSummary] unknown after={} withForm={} noForm={} mapOv={} top={}",
            norm.unknownAfterNormalize,
            norm.unknownWithFormID,
            norm.unknownNoFormID,
            norm.unknownMapOverflow,
            BuildUnknownTopSummary(norm));
        logger::info(
            "[Haptics][SessionSummary] unknownResolved {}",
            BuildUnknownTopResolvedSummary(norm));
        ExportUnknownOverrideTemplate(norm);

        logger::info(
            "[Haptics][SessionSummary] dynamicPool size={} admitted={} rejNoKey={} rejLow={} shadowHit={} shadowMiss={} l3Hit={} l3Miss={} rejMinHit={} rejLowIn={} l2Learn={} l2SkipNoKey={} l2SkipScore={}",
            dyn.currentSize,
            dyn.admitted,
            dyn.rejectedNoKey,
            dyn.rejectedLowConfidence,
            dyn.shadowHits,
            dyn.shadowMisses,
            dec.dynamicPoolHit,
            dec.dynamicPoolMiss,
            dyn.resolveRejectMinHits,
            dyn.resolveRejectLowInput,
            dec.dynamicPoolLearnFromL2,
            dec.dynamicPoolLearnFromL2NoKey,
            dec.dynamicPoolLearnFromL2LowScore);
        logger::info(
            "[Haptics][SessionSummary] sessionPromoter observe={} accepted={} promoteTry={} promoteHit={} promoteMiss={} entries={}",
            sessionPromoter.observeCalls,
            sessionPromoter.observedAccepted,
            sessionPromoter.promoteCalls,
            sessionPromoter.promoteHits,
            sessionPromoter.promoteMisses,
            sessionPromoter.entries);

        logger::info(
            "[Haptics][SessionSummary] io submitCalls={} pushed={} hidFrames={} hidFailures={} voiceDrop={} semanticRejectTotal={}",
            tap.submitCalls,
            tap.submitFeaturesPushed,
            hid.totalFramesSent,
            hid.sendFailures,
            voice.featuresDropped,
            semanticRejectTotal);
        logger::info(
            "[Haptics][SessionSummary] footBridge truths={} inst={} claims={} init={} submit={} miss(truth/inst)={}/{} deltaP50={}us deltaP95={}us samples={} pending(t/i/b)={}/{}/{}",
            footBridge.truthsObserved,
            footBridge.instancesObserved,
            footBridge.claimsMatched,
            footBridge.claimsFromInit,
            footBridge.claimsFromSubmit,
            footBridge.truthExpiredMisses,
            footBridge.instanceExpiredMisses,
            footBridge.deltaP50Us,
            footBridge.deltaP95Us,
            footBridge.deltaSamples,
            footBridge.pendingTruths,
            footBridge.pendingInstances,
            footBridge.activeBindings);
        logger::info(
            "[Haptics][SessionSummary] footAudio features={} truths={} matched={} bridge(bound/match/noFeat)={}/{}/{} noWin={} noSem={} lowScore={} cand(window/sem)={}/{} miss(bind/trace)={}/{} deltaP50={}us deltaP95={}us scoreP50={:.2f} scoreP95={:.2f} durP50={}us durP95={}us panAbsP50={:.2f} panAbsP95={:.2f} pending={}",
            footAudio.featuresObserved,
            footAudio.truthsObserved,
            footAudio.truthsMatched,
            footAudio.truthBridgeBound,
            footAudio.truthBridgeMatched,
            footAudio.truthBridgeNoFeature,
            footAudio.truthNoWindow,
            footAudio.truthNoSemantic,
            footAudio.truthLowScore,
            footAudio.windowCandidates,
            footAudio.semanticCandidates,
            footAudio.bindingMissCandidates,
            footAudio.traceMissCandidates,
            footAudio.matchDeltaP50Us,
            footAudio.matchDeltaP95Us,
            static_cast<float>(footAudio.matchScoreP50Permille) / 1000.0f,
            static_cast<float>(footAudio.matchScoreP95Permille) / 1000.0f,
            footAudio.matchDurationP50Us,
            footAudio.matchDurationP95Us,
            static_cast<float>(footAudio.matchPanAbsP50Permille) / 1000.0f,
            static_cast<float>(footAudio.matchPanAbsP95Permille) / 1000.0f,
            footAudio.pendingTruths);
        logger::info(
            "[Haptics][SessionSummary] footTruth total={} actorResolved={} player={} nonPlayer={} ctxAllow={} ctxBlock={} moving={} recent={} admissible={} shadow(match={} truthMiss={} renderMiss={} deltaP50={}us deltaP95={}us samples={} pending={})",
            footTruth.totalEvents,
            footTruth.actorResolvedEvents,
            footTruth.playerEvents,
            footTruth.nonPlayerEvents,
            footTruth.contextAllowedEvents,
            footTruth.contextBlockedEvents,
            footTruth.movingEvents,
            footTruth.recentMoveEvents,
            footTruth.admissibleEvents,
            footTruth.shadowMatchedRenders,
            footTruth.shadowExpiredTruthMisses,
            footTruth.shadowRenderWithoutTruth,
            footTruth.shadowRenderDeltaP50Us,
            footTruth.shadowRenderDeltaP95Us,
            footTruth.shadowRenderDeltaSamples,
            footTruth.shadowPendingTruth);
        logger::info(
            "[Haptics][SessionSummary] tx q(FG/BG)={}/{} dq(FG/BG)={}/{} dropFull(FG/BG)={}/{} dropStale(FG/BG)={}/{} merge(FG/BG)={}/{} sendOk={} sendFail={} noDev={} depth(FG/BG)={}/{} renderP50={}us renderP95={}us renderN={} firstP50={}us firstP95={}us firstN={} skipRpt={} stopFlush={} route(fg/bg={}/{}, fgZero/hint/prio/evt={}/{}/{}/{}, bgUnk/bgEvt={}/{}) select(forceFg/bgWhileFg={}/{}) state(upFg/upBg={}/{}, ovFg/ovBg={}/{}, carryQ(FG/BG)={}/{}, carryDrop(FG/BG)={}/{}, carryUse(FG/BG)={}/{}, expDrop={} futSkip={})",
            hid.txQueuedFg,
            hid.txQueuedBg,
            hid.txDequeuedFg,
            hid.txDequeuedBg,
            hid.txDropQueueFullFg,
            hid.txDropQueueFullBg,
            hid.txDropStaleFg,
            hid.txDropStaleBg,
            hid.txMergedFg,
            hid.txMergedBg,
            hid.txSendOk,
            hid.txSendFail,
            hid.txNoDevice,
            hid.txQueueDepthFg,
            hid.txQueueDepthBg,
            hid.txRenderOverP50Us,
            hid.txRenderOverP95Us,
            hid.txRenderOverSamples,
            hid.txFirstRenderP50Us,
            hid.txFirstRenderP95Us,
            hid.txFirstRenderSamples,
            hid.txSkippedRepeat,
            hid.txStopFlushes,
            hid.txRouteFg,
            hid.txRouteBg,
            hid.txRouteFgZero,
            hid.txRouteFgHint,
            hid.txRouteFgPriority,
            hid.txRouteFgEvent,
            hid.txRouteBgUnknown,
            hid.txRouteBgBackground,
            hid.txSelectForcedFgBudget,
            hid.txSelectBgWhileFgPending,
            hid.txStateUpdateFg,
            hid.txStateUpdateBg,
            hid.txStateOverwriteFg,
            hid.txStateOverwriteBg,
            hid.txStateCarryQueuedFg,
            hid.txStateCarryQueuedBg,
            hid.txStateCarryDropFg,
            hid.txStateCarryDropBg,
            hid.txStateCarryConsumedFg,
            hid.txStateCarryConsumedBg,
            hid.txStateExpiredDrop,
            hid.txStateFutureSkip);
        logger::info(
            "[Haptics][SessionSummary] mixer sources={} clampUnk={} clampBg={} bgIso={} fgFam={} dropEvt={} dropUnkLow={} dropUnkSem={} budgetDrop={} activeFg={} activeBg={} meta(active/carrier)={}/{} dropOutUnk={}",
            mixer.totalSourcesAdded,
            mixer.softClampUnknown,
            mixer.softClampBackground,
            mixer.dropBackgroundHardIsolated,
            mixer.foregroundFamilies,
            mixer.dropEventDisabled,
            mixer.dropUnknownLowInput,
            mixer.dropUnknownSemantic,
            mixer.budgetDropCount,
            mixer.activeForeground,
            mixer.activeBackground,
            mixer.outputMetaFromActive,
            mixer.outputMetaFromCarrier,
            mixer.outputDropUnknownNonZero);
        logger::info(
            "[Haptics][SessionSummary] mixerProbe add(call={} ins={} mergeForm={} mergeLock={} dropUnkBudget={} lateRescue={} bgIso={}) age(sample={} <8ms={} 8-20={} 20-50={} 50-100={} 100+={} max={}us) budget(unk={} fg={} bg={} fgFamilies={})",
            mixer.sourceAddCalls,
            mixer.sourceInserted,
            mixer.sourceMergedSameForm,
            mixer.sourceMergedAudioLock,
            mixer.sourceDropUnknownBudget,
            mixer.sourceLateRescue,
            mixer.dropBackgroundHardIsolated,
            mixer.sourceAgeSamples,
            mixer.sourceAgeLt8Ms,
            mixer.sourceAge8To20Ms,
            mixer.sourceAge20To50Ms,
            mixer.sourceAge50To100Ms,
            mixer.sourceAge100MsPlus,
            mixer.sourceAgeMaxUs,
            mixer.budgetDropUnknown,
            mixer.budgetDropForeground,
            mixer.budgetDropBackground,
            mixer.foregroundFamilies);
        logger::info("[Haptics][SessionSummary] ========================================");
    }

    bool HapticsSystem::InitializeConfig()
    {
        logger::info("[Haptics][System] [1/3] Loading configuration...");

        auto& config = HapticsConfig::GetSingleton();
        config.Load();

        if (!config.enabled) {
            logger::warn("[Haptics][System] Haptics disabled in config");
            return false;
        }

        logger::info("[Haptics][System] Config loaded: tick={}ms queue={} minConf={:.2f} mode={}",
            config.tickMs, config.queueCapacity, config.minConfidence,
            static_cast<int>(config.hapticsMode));

        return true;
    }

    bool HapticsSystem::InitializeCorePipeline()
    {
        logger::info("[Haptics][System] [2/3] Initializing audio core pipeline...");
        VoiceManager::GetSingleton().Initialize();
        logger::info("[Haptics][System] Audio core pipeline initialized");
        return true;
    }

    bool HapticsSystem::InitializeThreads()
    {
        logger::info("[Haptics][System] [3/3] Starting threads...");
        HapticMixer::GetSingleton().Start();
        logger::info("[Haptics][System] Threads started");
        return true;
    }

    void HapticsSystem::StopThreads()
    {
        logger::info("[Haptics][System] Stopping threads...");
        HapticMixer::GetSingleton().Stop();
        logger::info("[Haptics][System] Threads stopped");
    }

    void HapticsSystem::ShutdownCorePipeline()
    {
        logger::info("[Haptics][System] Shutting down audio core pipeline...");
        VoiceManager::GetSingleton().Shutdown();
        logger::info("[Haptics][System] Audio core pipeline shutdown");
    }
}
