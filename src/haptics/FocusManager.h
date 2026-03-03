#pragma once

#include "haptics/HapticsTypes.h"
#include <chrono>
#include <vector>
#include <mutex>

namespace dualpad::haptics
{
    class FocusManager
    {
    public:
        FocusManager() = default;
        ~FocusManager() = default;

        // 设置焦点
        void SetFocus(EventType type, std::uint32_t durationMs);

        // 更新焦点状态（每帧调用）
        void Update();

        // 查询焦点状态
        bool HasFocus() const;
        EventType GetCurrentFocus() const;
        std::uint32_t GetRemainingMs() const;

        // 查询压制系数
        float GetDuckFactorFor(EventType targetType) const;

        // 设置 ducking 规则
        void SetDuckingRules(const std::vector<DuckingRule>& rules);

    private:
        using Clock = std::chrono::steady_clock;
        using TimePoint = std::chrono::steady_clock::time_point;

        EventType _currentFocus{ EventType::Unknown };
        TimePoint _focusExpireTime;
        std::vector<DuckingRule> _duckingRules;

        mutable std::mutex _mutex;
    };
}