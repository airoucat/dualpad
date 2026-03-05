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
#include "haptics/MetricsReporter.h"
#include "haptics/PlayPathHook.h"
#include "haptics/AudioOnlyScorer.h"
#include "haptics/FormSemanticCache.h"
#include "haptics/SemanticResolver.h"

#include <SKSE/SKSE.h>
#include <algorithm>

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

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Initializing Haptics System...");
        logger::info("[Haptics][System] ========================================");

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
            logger::info("[Haptics][System] ========================================");
            logger::info("[Haptics][System] Haptics System Initialized (NativeOnly)");
            logger::info("[Haptics][System] ========================================");
            return true;
        }

        if (!InitializeCorePipeline()) {
            logger::error("[Haptics][System] Failed to initialize core pipeline");
            return false;
        }

        _corePipelineInitialized.store(true, std::memory_order_release);
        _initialized = true;

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Haptics System Initialized (CustomAudio)");
        logger::info("[Haptics][System] ========================================");
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

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Starting Haptics System...");
        logger::info("[Haptics][System] ========================================");

        auto& config = HapticsConfig::GetSingleton();
        if (config.IsNativeOnly()) {
            _customPipelineActive.store(false, std::memory_order_release);

            logger::info("[Haptics][System] Mode=NativeOnly");
            logger::info("[Haptics][System] Custom pipeline disabled: no tap / no mixer / no custom HID output");
            logger::info("[Haptics][System] ========================================");
            logger::info("[Haptics][System] Haptics System Started (NativeOnly Mode)");
            logger::info("[Haptics][System] ========================================");
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

        _customPipelineActive.store(true, std::memory_order_release);

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Haptics System Started (CustomAudio)");
        PrintStats();
        logger::info("[Haptics][System] ========================================");
        return true;
    }

    void HapticsSystem::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Stopping Haptics System...");
        logger::info("[Haptics][System] ========================================");

        const bool customActive = _customPipelineActive.exchange(false, std::memory_order_acq_rel);
        if (customActive) {
            StopThreads();
            PrintSessionSummary();
            HidOutput::GetSingleton().StopVibration();
            EngineAudioTap::Uninstall();
        }

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Haptics System Stopped");
        logger::info("[Haptics][System] ========================================");
    }

    void HapticsSystem::Shutdown()
    {
        if (!_initialized) {
            return;
        }

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Shutting down Haptics System...");
        logger::info("[Haptics][System] ========================================");

        Stop();

        if (_corePipelineInitialized.exchange(false, std::memory_order_acq_rel)) {
            ShutdownCorePipeline();
        }

        _initialized = false;

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Haptics System Shutdown Complete");
        logger::info("[Haptics][System] ========================================");
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

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Haptics System Statistics (AudioOnly)");
        logger::info("[Haptics][System] ========================================");

        auto tapStats = EngineAudioTap::GetStats();
        logger::info("[Haptics][SubmitTap] calls={} feature_pushed={} compressed_skip={}",
            tapStats.submitCalls,
            tapStats.submitFeaturesPushed,
            tapStats.submitCompressedSkipped);

        auto voiceStats = VoiceManager::GetSingleton().GetStats();
        logger::info("[Haptics][VoiceManager] pushed={} dropped={}",
            voiceStats.featuresPushed, voiceStats.featuresDropped);

        auto playStats = PlayPathHook::GetSingleton().GetStats();
        logger::info("[Haptics][PlayPath] initCalls={} initResolved={} submitCalls={} submitResolved={} noContext={} noForm={} skipResolved={} bindMiss={} upserts={}",
            playStats.initCalls,
            playStats.initResolved,
            playStats.submitCalls,
            playStats.submitResolved,
            playStats.submitNoContext,
            playStats.submitNoForm,
            playStats.submitSkipResolved,
            playStats.bindingMisses,
            playStats.traceUpserts);

        auto audioStats = AudioOnlyScorer::GetSingleton().GetStats();
        logger::info("[Haptics][AudioOnlyScorer] pulled={} produced={} lowEnergyDropped={}",
            audioStats.featuresPulled,
            audioStats.sourcesProduced,
            audioStats.lowEnergyDropped);

        auto normStats = EventNormalizer::GetSingleton().GetStats();
        logger::info("[Haptics][EventNormalizer] inputs={} patchForm={} patchEvent={} bindMiss={} traceMiss={}",
            normStats.inputs,
            normStats.patchedFormID,
            normStats.patchedEventType,
            normStats.bindingMiss,
            normStats.traceMiss);

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
        auto dynamicStats = DynamicHapticPool::GetSingleton().GetStats();
        logger::info(
            "[Haptics][DynamicPool] size={} observe={} admitted={} rejNoKey={} rejLow={} shCall={} shHit={} shMiss={} hit={} miss={} evicted={} learnL2={} learnNoKey={} learnLowScore={}",
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
        logger::info("[Haptics][Mixer] Active={} Frames={} Ticks={} Sources={} PeakL={:.3f} PeakR={:.3f}",
            mixerStats.activeSources,
            mixerStats.framesOutput,
            mixerStats.totalTicks,
            mixerStats.totalSourcesAdded,
            mixerStats.peakLeft,
            mixerStats.peakRight);

        auto hidStats = HidOutput::GetSingleton().GetStats();
        logger::info("[Haptics][HidOutput] Frames={} Bytes={} Failures={}",
            hidStats.totalFramesSent, hidStats.totalBytesSent, hidStats.sendFailures);

        logger::info("[Haptics][System] ========================================");
    }

    void HapticsSystem::PrintSessionSummary()
    {
        auto tap = EngineAudioTap::GetStats();
        auto dec = DecisionEngine::GetSingleton().GetStats();
        auto play = PlayPathHook::GetSingleton().GetStats();
        auto norm = EventNormalizer::GetSingleton().GetStats();
        auto dyn = DynamicHapticPool::GetSingleton().GetStats();
        auto voice = VoiceManager::GetSingleton().GetStats();
        auto hid = HidOutput::GetSingleton().GetStats();

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
            "[Haptics][SessionSummary] noHit tickNoAudio={} audioNoMatch={} traceMiss={} (unbound={} expired={} disabled={})",
            dec.tickNoAudio,
            dec.audioPresentNoMatch,
            traceMissTotal,
            dec.traceBindMissUnbound,
            dec.traceBindMissExpired,
            dec.traceBindBypassDisabled);

        logger::info(
            "[Haptics][SessionSummary] semantic miss={} (noForm={} cacheMiss={} lowConf={})",
            dec.l1FormSemanticMiss,
            dec.l1FormSemanticNoFormID,
            dec.l1FormSemanticCacheMiss,
            dec.l1FormSemanticLowConfidence);

        logger::info(
            "[Haptics][SessionSummary] playPath initResolved={}/{} ({:.1f}%) submitResolved={}/{} ({:.1f}%) patchForm={} patchEvent={} traceMiss={} bindMiss={}",
            play.initResolved,
            play.initCalls,
            PercentOf(play.initResolved, play.initCalls),
            play.submitResolved,
            play.submitCalls,
            PercentOf(play.submitResolved, play.submitCalls),
            norm.patchedFormID,
            norm.patchedEventType,
            norm.traceMiss,
            norm.bindingMiss);

        logger::info(
            "[Haptics][SessionSummary] dynamicPool size={} admitted={} rejNoKey={} rejLow={} shadowHit={} shadowMiss={} l3Hit={} l3Miss={} l2Learn={} l2SkipNoKey={} l2SkipScore={}",
            dyn.currentSize,
            dyn.admitted,
            dyn.rejectedNoKey,
            dyn.rejectedLowConfidence,
            dyn.shadowHits,
            dyn.shadowMisses,
            dec.dynamicPoolHit,
            dec.dynamicPoolMiss,
            dec.dynamicPoolLearnFromL2,
            dec.dynamicPoolLearnFromL2NoKey,
            dec.dynamicPoolLearnFromL2LowScore);

        logger::info(
            "[Haptics][SessionSummary] io submitCalls={} pushed={} hidFrames={} hidFailures={} voiceDrop={} semanticRejectTotal={}",
            tap.submitCalls,
            tap.submitFeaturesPushed,
            hid.totalFramesSent,
            hid.sendFailures,
            voice.featuresDropped,
            semanticRejectTotal);
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
