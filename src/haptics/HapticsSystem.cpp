#include "pch.h"
#include "haptics/HapticsSystem.h"

#include "haptics/HapticsConfig.h"
#include "haptics/VoiceManager.h"
#include "haptics/HapticMixer.h"
#include "haptics/HidOutput.h"
#include "haptics/EngineAudioTap.h"
#include "haptics/AudioOnlyScorer.h"
#include "haptics/FormSemanticCache.h"

#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
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

        auto audioStats = AudioOnlyScorer::GetSingleton().GetStats();
        logger::info("[Haptics][AudioOnlyScorer] pulled={} produced={} lowEnergyDropped={}",
            audioStats.featuresPulled,
            audioStats.sourcesProduced,
            audioStats.lowEnergyDropped);

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
