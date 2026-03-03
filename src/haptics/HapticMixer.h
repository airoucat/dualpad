#pragma once
#include "haptics/HapticsTypes.h"
#include "haptics/FocusManager.h"
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace dualpad::haptics
{
    // 触觉源（带生存期）
    struct ActiveSource
    {
        HapticSourceMsg msg;
        TimePoint expireTime;
        float currentLeft{ 0.0f };
        float currentRight{ 0.0f };
    };

    // 混音器线程
    // 固定 tick 运行，聚合多个源，输出到 HID
    class HapticMixer
    {
    public:
        static HapticMixer& GetSingleton();

        // 启动混音器线程
        void Start();

        // 停止混音线程
        void Stop();

        // 添加触觉源（由 EventWindowScorer 调用）
        void AddSource(const HapticSourceMsg& msg);

        // 查询状态
        bool IsRunning() const { return _running.load(std::memory_order_acquire); }

        // 统计
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

        // 上一帧输出（用于 slew rate limiting）
        float _lastLeft{ 0.0f };
        float _lastRight{ 0.0f };

        // 新增：焦点管理器
        FocusManager _focusManager;

        // 统计
        std::atomic<std::uint32_t> _totalTicks{ 0 };
        std::atomic<std::uint32_t> _totalSourcesAdded{ 0 };
        std::atomic<std::uint32_t> _framesOutput{ 0 };
        std::atomic<float> _peakLeft{ 0.0f };
        std::atomic<float> _peakRight{ 0.0f };

        // 混音线程主循环
        void MixerThreadLoop();

        // 单次 tick 处理
        HidFrame ProcessTick();

        // 处理步骤
        void UpdateActiveSources();
        void MixSources(float& outLeft, float& outRight);
        void ApplyDucking(float& left, float& right);
        void ApplyCompressor(float& left, float& right);
        void ApplyLimiter(float& left, float& right);
        void ApplySlewLimit(float& left, float& right);
        void ApplyDeadzone(float& left, float& right);

        // 辅助
        float GetDuckingFactor(SourceType type) const;
        bool HasHighPrioritySource() const;
        EventType GetEventTypeFromSource(const ActiveSource& src) const;
    };
}