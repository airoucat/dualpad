#pragma once
#include "haptics/HapticsTypes.h"
#include "haptics/FocusManager.h"

#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace dualpad::haptics
{
    struct ActiveSource
    {
        HapticSourceMsg msg;
        TimePoint holdUntil;
        TimePoint releaseEndTime;
        float currentLeft{ 0.0f };
        float currentRight{ 0.0f };
        std::uint64_t lockKey{ 0 };
    };

    class HapticMixer
    {
    public:
        static HapticMixer& GetSingleton();

        void Start();
        void Stop();

        void AddSource(const HapticSourceMsg& msg);

        bool IsRunning() const { return _running.load(std::memory_order_acquire); }

        struct Stats
        {
            std::uint32_t totalTicks{ 0 };
            std::uint32_t totalSourcesAdded{ 0 };
            std::uint32_t activeSources{ 0 };
            std::uint32_t framesOutput{ 0 };
            std::uint32_t softClampUnknown{ 0 };
            std::uint32_t softClampBackground{ 0 };
            std::uint32_t dropEventDisabled{ 0 };
            std::uint32_t dropUnknownLowInput{ 0 };
            std::uint32_t dropUnknownSemantic{ 0 };
            std::uint32_t budgetDropCount{ 0 };
            std::uint32_t activeForeground{ 0 };
            std::uint32_t activeBackground{ 0 };
            std::uint32_t audioLockMerged{ 0 };
            std::uint32_t audioLockCreated{ 0 };
            std::uint32_t audioLockRejectedStart{ 0 };
            std::uint32_t sourceAddCalls{ 0 };
            std::uint32_t sourceInserted{ 0 };
            std::uint32_t sourceMergedSameForm{ 0 };
            std::uint32_t sourceMergedAudioLock{ 0 };
            std::uint32_t sourceDropUnknownBudget{ 0 };
            std::uint32_t sourceLateRescue{ 0 };
            std::uint32_t sourceAgeSamples{ 0 };
            std::uint32_t sourceAgeLt8Ms{ 0 };
            std::uint32_t sourceAge8To20Ms{ 0 };
            std::uint32_t sourceAge20To50Ms{ 0 };
            std::uint32_t sourceAge50To100Ms{ 0 };
            std::uint32_t sourceAge100MsPlus{ 0 };
            std::uint32_t sourceAgeMaxUs{ 0 };
            std::uint32_t budgetDropUnknown{ 0 };
            std::uint32_t budgetDropForeground{ 0 };
            std::uint32_t budgetDropBackground{ 0 };
            std::uint32_t dropBackgroundHardIsolated{ 0 };
            std::uint32_t foregroundFamilies{ 0 };
            std::uint32_t outputMetaFromActive{ 0 };
            std::uint32_t outputMetaFromCarrier{ 0 };
            std::uint32_t outputDropUnknownNonZero{ 0 };
            float avgTickTimeUs{ 0.0f };
            float peakLeft{ 0.0f };
            float peakRight{ 0.0f };
        };

        Stats GetStats() const;

    private:
        struct FrameAttribution
        {
            bool valid{ false };
            EventType eventType{ EventType::Unknown };
            SourceType sourceType{ SourceType::AudioMod };
            float confidence{ 0.0f };
            std::uint8_t priority{ 0 };
            std::uint32_t sourceFormId{ 0 };
            std::uint8_t flags{ HapticSourceFlagNone };
        };

        struct OutputCarrier
        {
            bool valid{ false };
            EventType eventType{ EventType::Unknown };
            SourceType sourceType{ SourceType::AudioMod };
            float confidence{ 0.0f };
            std::uint8_t priority{ 0 };
            std::uint32_t sourceFormId{ 0 };
            std::uint8_t flags{ HapticSourceFlagNone };
            std::uint64_t expireUs{ 0 };
        };

        struct EventLeaseState
        {
            bool active{ false };
            EventType eventType{ EventType::Unknown };
            SourceType sourceType{ SourceType::AudioMod };
            float targetLeft{ 0.0f };
            float targetRight{ 0.0f };
            float valueLeft{ 0.0f };
            float valueRight{ 0.0f };
            float confidence{ 0.0f };
            std::uint8_t priority{ 0 };
            std::uint32_t sourceFormId{ 0 };
            std::uint8_t flags{ HapticSourceFlagNone };
            std::uint64_t holdUntilUs{ 0 };
            std::uint64_t releaseEndUs{ 0 };
        };

        HapticMixer() = default;

        std::atomic<bool> _running{ false };
        std::jthread _thread;

        mutable std::mutex _mutex;
        std::vector<ActiveSource> _activeSources;

        float _lastLeft{ 0.0f };
        float _lastRight{ 0.0f };

        FocusManager _focusManager;

        std::atomic<std::uint32_t> _totalTicks{ 0 };
        std::atomic<std::uint32_t> _totalSourcesAdded{ 0 };
        std::atomic<std::uint32_t> _framesOutput{ 0 };
        std::atomic<std::uint32_t> _softClampUnknown{ 0 };
        std::atomic<std::uint32_t> _softClampBackground{ 0 };
        std::atomic<std::uint32_t> _dropEventDisabled{ 0 };
        std::atomic<std::uint32_t> _dropUnknownLowInput{ 0 };
        std::atomic<std::uint32_t> _dropUnknownSemantic{ 0 };
        std::atomic<std::uint32_t> _budgetDropCount{ 0 };
        std::atomic<std::uint32_t> _activeForeground{ 0 };
        std::atomic<std::uint32_t> _activeBackground{ 0 };
        std::atomic<std::uint32_t> _audioLockMerged{ 0 };
        std::atomic<std::uint32_t> _audioLockCreated{ 0 };
        std::atomic<std::uint32_t> _audioLockRejectedStart{ 0 };
        std::atomic<std::uint32_t> _sourceAddCalls{ 0 };
        std::atomic<std::uint32_t> _sourceInserted{ 0 };
        std::atomic<std::uint32_t> _sourceMergedSameForm{ 0 };
        std::atomic<std::uint32_t> _sourceMergedAudioLock{ 0 };
        std::atomic<std::uint32_t> _sourceDropUnknownBudget{ 0 };
        std::atomic<std::uint32_t> _sourceLateRescue{ 0 };
        std::atomic<std::uint32_t> _sourceAgeSamples{ 0 };
        std::atomic<std::uint32_t> _sourceAgeLt8Ms{ 0 };
        std::atomic<std::uint32_t> _sourceAge8To20Ms{ 0 };
        std::atomic<std::uint32_t> _sourceAge20To50Ms{ 0 };
        std::atomic<std::uint32_t> _sourceAge50To100Ms{ 0 };
        std::atomic<std::uint32_t> _sourceAge100MsPlus{ 0 };
        std::atomic<std::uint32_t> _sourceAgeMaxUs{ 0 };
        std::atomic<std::uint32_t> _budgetDropUnknown{ 0 };
        std::atomic<std::uint32_t> _budgetDropForeground{ 0 };
        std::atomic<std::uint32_t> _budgetDropBackground{ 0 };
        std::atomic<std::uint32_t> _dropBackgroundHardIsolated{ 0 };
        std::atomic<std::uint32_t> _foregroundFamilies{ 0 };
        std::atomic<std::uint32_t> _outputMetaFromActive{ 0 };
        std::atomic<std::uint32_t> _outputMetaFromCarrier{ 0 };
        std::atomic<std::uint32_t> _outputDropUnknownNonZero{ 0 };
        std::atomic<float> _peakLeft{ 0.0f };
        std::atomic<float> _peakRight{ 0.0f };
        std::uint64_t _unknownBudgetWindowUs{ 0 };
        std::uint32_t _unknownStructuredUsed{ 0 };
        std::uint32_t _unknownUnstructuredUsed{ 0 };
        std::uint64_t _probeWindowUs{ 0 };
        std::uint32_t _probeLinesInWindow{ 0 };
        OutputCarrier _outputCarrier{};
        EventLeaseState _eventLease{};

        void MixerThreadLoop();
        HidFrame ProcessTick();

        void UpdateActiveSources();
        void MixSources(float& outLeft, float& outRight, FrameAttribution* outAttribution);
        void ApplyDucking(float& left, float& right);
        void ApplyCompressor(float& left, float& right);
        void ApplyLimiter(float& left, float& right);
        void ApplySlewLimit(float& left, float& right);
        void ApplyDeadzone(float& left, float& right);
        void RefreshEventLease(const FrameAttribution& attribution, float left, float right, std::uint64_t nowUs);
        void ApplyEventLease(float& left, float& right, FrameAttribution& attribution, std::uint64_t nowUs);

        float GetDuckingFactor(SourceType type) const;
        bool HasHighPrioritySource() const;
        EventType GetEventTypeFromSource(const ActiveSource& src) const;
    };
}
