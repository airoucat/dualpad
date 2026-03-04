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
        TimePoint expireTime;
        float currentLeft{ 0.0f };
        float currentRight{ 0.0f };
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
            float avgTickTimeUs{ 0.0f };
            float peakLeft{ 0.0f };
            float peakRight{ 0.0f };
        };

        Stats GetStats() const;

    private:
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
        std::atomic<float> _peakLeft{ 0.0f };
        std::atomic<float> _peakRight{ 0.0f };

        void MixerThreadLoop();
        HidFrame ProcessTick();

        void UpdateActiveSources();
        void MixSources(float& outLeft, float& outRight);
        void ApplyDucking(float& left, float& right);
        void ApplyCompressor(float& left, float& right);
        void ApplyLimiter(float& left, float& right);
        void ApplySlewLimit(float& left, float& right);
        void ApplyDeadzone(float& left, float& right);

        float GetDuckingFactor(SourceType type) const;
        bool HasHighPrioritySource() const;
        EventType GetEventTypeFromSource(const ActiveSource& src) const;
    };
}