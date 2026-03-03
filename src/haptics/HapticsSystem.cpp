#include "pch.h"
#include "haptics/HapticsSystem.h"
#include "haptics/HapticsConfig.h"
#include "haptics/SemanticWarmupService.h"
#include "haptics/VoiceManager.h"
#include "haptics/EventQueue.h"
#include "haptics/EventWindowScorer.h"
#include "haptics/HapticMixer.h"
#include "haptics/HidOutput.h"
#include "haptics/EngineAudioTap.h"
#include "haptics/FormSemanticCache.h"
#include "haptics/HapticTemplateCache.h"

#include <SKSE/SKSE.h>
#include <atomic>

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

        if (!InitializeQueues()) {
            logger::error("[Haptics][System] Failed to initialize queues");
            return false;
        }

        if (!InitializeManagers()) {
            logger::error("[Haptics][System] Failed to initialize managers");
            return false;
        }

        if (!EngineAudioTap::Install()) {
            logger::error("[Haptics][System] EngineAudioTap install failed");
            return false;
        }

        _initialized = true;

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Haptics System Initialized");
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

        if (config.hapticsMode == HapticsConfig::HapticsMode::NativeOnly) {
            logger::info("[Haptics][System] Mode is NativeOnly, skipping custom haptics");
            logger::info("[Haptics][System] ========================================");
            logger::info("[Haptics][System] Haptics System Started (NativeOnly Mode)");
            logger::info("[Haptics][System] ========================================");
            return true;
        }

        if (!InitializeThreads()) {
            logger::error("[Haptics][System] Failed to start threads");
            _running.store(false);
            return false;
        }

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Haptics System Started (Mode: {})",
            static_cast<int>(config.hapticsMode));
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

        StopThreads();
        HidOutput::GetSingleton().StopVibration();

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
        EngineAudioTap::Uninstall();
        ShutdownManagers();

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

        logger::info("[Haptics][System] ========================================");
        logger::info("[Haptics][System] Haptics System Statistics");
        logger::info("[Haptics][System] ========================================");

        auto tapStats = EngineAudioTap::GetStats();
        logger::info("[Haptics][SubmitTap] calls={} feature_pushed={} compressed_skip={}",
            tapStats.submitCalls,
            tapStats.submitFeaturesPushed,
            tapStats.submitCompressedSkipped);

        auto voiceStats = VoiceManager::GetSingleton().GetStats();
        logger::info("[Haptics][VoiceManager] pushed={} dropped={}",
            voiceStats.featuresPushed, voiceStats.featuresDropped);

        auto scorerStats = EventWindowScorer::GetSingleton().GetStats();
        logger::info("[Haptics][Scorer] Pulled audio features={} Produced sources={} Passthrough={} Events={} Matched={} Unmatched={} Active={}",
            scorerStats.audioFeaturesPulled,
            scorerStats.sourcesProduced,
            scorerStats.passthroughSources,
            scorerStats.totalEvents,
            scorerStats.totalMatched,
            scorerStats.totalUnmatched,
            scorerStats.activeWindows);

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
        logger::info("[Haptics][System] [1/4] Loading configuration...");

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

    bool HapticsSystem::InitializeQueues()
    {
        logger::info("[Haptics][System] [2/4] Initializing queues...");
        VoiceManager::GetSingleton().Initialize();
        EventQueue::GetSingleton().Initialize();
        logger::info("[Haptics][System] Queues initialized");
        return true;
    }

    bool HapticsSystem::InitializeManagers()
    {
        logger::info("[Haptics][System] [3/4] Initializing managers...");

        FormSemanticCache::GetSingleton().WarmupDefaults();
        HapticTemplateCache::GetSingleton().WarmupDefaults();

        // 新增：全量声音相关 Form 语义预热（加载或重建）
        (void)SemanticWarmupService::GetSingleton().Boot();

        EventWindowScorer::GetSingleton().Initialize();

        logger::info("[Haptics][System] Managers initialized");
        return true;
    }

    bool HapticsSystem::InitializeThreads()
    {
        logger::info("[Haptics][System] [4/4] Starting threads...");
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

    void HapticsSystem::ShutdownManagers()
    {
        logger::info("[Haptics][System] Shutting down managers...");
        EventWindowScorer::GetSingleton().Shutdown();
        VoiceManager::GetSingleton().Shutdown();
        logger::info("[Haptics][System] Managers shutdown");
    }
}