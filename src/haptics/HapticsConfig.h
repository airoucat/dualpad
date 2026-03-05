#pragma once
#include "haptics/HapticsTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace dualpad::haptics
{
    struct HapticsConfig
    {
        enum class HapticsMode
        {
            NativeOnly,
            Hybrid,
            AudioDriven
        };

        struct EventConfig
        {
            std::uint8_t priority{ 50 };
            std::uint32_t ttlMs{ 100 };
            std::uint32_t focusWindowMs{ 0 };
            float duckingStrength{ 0.0f };
            bool allowDucking{ true };
            bool requiresAudio{ false };
        };

        // Core
        bool enabled{ true };
        std::uint32_t tickMs{ 4 };
        bool useQpc{ true };
        bool hotReload{ true };

        // Mode
        HapticsMode hapticsMode{ HapticsMode::AudioDriven };
        bool allowSubtleEventsWithoutAudio{ false };

        // AudioTap
        std::uint32_t blockPreferSamples{ 128 };
        std::uint32_t queueCapacity{ 4096 };

        // Scoring
        std::uint32_t windowPreMs{ 20 };
        std::uint32_t windowPostMs{ 80 };
        float minConfidence{ 0.35f };
        float weightTiming{ 0.45f };
        float weightAttack{ 0.20f };
        float weightMeta{ 0.25f };
        float weightSpectrum{ 0.20f };
        float weightDuration{ 0.10f };
        float weightChannel{ 0.05f };

        // Mixer
        float limiter{ 0.90f };
        std::uint32_t compressorAttackMs{ 3 };
        std::uint32_t compressorReleaseMs{ 40 };
        float slewPerTick{ 30.0f };
        float deadzone{ 0.03f };

        // Ducking
        float hitDuckFootstep{ 0.30f };
        float hitDuckAmbient{ 0.20f };
        float spellDuckFootstep{ 0.60f };

        // Priority
        int priorityHit{ 100 };
        int prioritySpell{ 80 };
        int prioritySwing{ 70 };
        int priorityFootstep{ 60 };
        int priorityAmbient{ 30 };

        // Fallback/Mode refine
        bool enableBasePulse{ true };
        float basePulseHit{ 0.85f };
        float basePulseSwing{ 0.65f };
        float basePulseFootstep{ 0.40f };
        bool audioDrivenPreferAudioOnly{ true };
        bool fallbackBaseWhenNoMatch{ true };

        // Dynamic pool (session-only Top-K)
        bool enableDynamicHapticPool{ true };
        std::uint32_t dynamicPoolTopK{ 64 };
        float dynamicPoolMinConfidence{ 0.62f };
        float dynamicPoolOutputCap{ 0.75f };
        bool enableDynamicPoolShadowProbe{ true };
        bool enableDynamicPoolLearnFromL2{ false };
        float dynamicPoolL2MinConfidence{ 0.62f };
        std::uint32_t dynamicPoolResolveMinHits{ 2 };
        float dynamicPoolResolveMinInputEnergy{ 0.04f };

        // Device
        std::string outputBackend{ "hid" };
        std::uint32_t retryCount{ 3 };
        std::uint32_t reconnectMs{ 500 };

        // LowLatency
        std::uint32_t correctionWindowMs{ 30 };
        std::uint32_t eventShortWindowMs{ 180 };
        std::uint32_t submitFeatureCacheMs{ 420 };
        std::uint32_t traceBindingTtlMs{ 1500 };
        float correctionMinScore{ 0.38f };
        float immediateGain{ 1.00f };
        float correctionGain{ 1.00f };
        bool enableAmbientPassthrough{ false };

        // Semantic cache
        bool enableFormSemanticCache{ true };
        bool enableL1FormSemantic{ true };
        bool enableL1VoiceTrace{ true };
        bool enableSubmitNoContextFallback{ true };
        bool enableSubmitNoContextDeepFallback{ true };
        float l1FormSemanticMinConfidence{ 0.70f };
        std::uint32_t submitSemanticScanMaxAttempts{ 3 };
        std::uint32_t submitSemanticRetryIntervalMs{ 120 };
        std::string semanticRulesPath{ "Data/SKSE/Plugins/DualPadSemanticRules.json" };
        std::string semanticCachePath{ "Data/SKSE/Plugins/DualPadSemanticCache.bin" };
        bool semanticForceRebuild{ false };

        // Debug
        std::string logLevel{ "info" };
        std::uint32_t statsIntervalMs{ 1000 };
        bool dumpMetrics{ false };

        // Extension API
        bool enableCustomEvents{ true };
        std::uint32_t maxCustomEvents{ 32 };
        std::uint8_t customEventPriorityBase{ 50 };

        std::unordered_map<EventType, EventConfig> eventConfigs;
        std::vector<DuckingRule> duckingRules;

        static HapticsConfig& GetSingleton();
        bool Load(const std::filesystem::path& path = {});
        static std::filesystem::path GetDefaultPath();

        bool IsEventAllowed(EventType type) const;
        const EventConfig* GetEventConfig(EventType type) const;
        EventType StringToEventType(const std::string& str) const;

        bool IsNativeOnly() const { return hapticsMode == HapticsMode::NativeOnly; }
        bool IsCustomAudioMode() const { return hapticsMode != HapticsMode::NativeOnly; }

    private:
        HapticsConfig() = default;

        bool ParseIniFile(const std::filesystem::path& path);

        void LoadLowLatencyConfig(const std::unordered_map<std::string, std::string>& values);
        void LoadSemanticConfig(const std::unordered_map<std::string, std::string>& values);
        void LoadModeConfig(const std::unordered_map<std::string, std::string>& values);
        void LoadEventPriorityConfig(const std::unordered_map<std::string, std::string>& values);
        void LoadDuckingMatrix(const std::unordered_map<std::string, std::string>& values);
        void LoadDynamicPoolConfig(const std::unordered_map<std::string, std::string>& values);
        void LoadExtensionConfig(const std::unordered_map<std::string, std::string>& values);
    };
}
