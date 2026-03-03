#include "pch.h"
#include "haptics/FocusManager.h"
#include <SKSE/SKSE.h>
#include <algorithm>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    void FocusManager::SetFocus(EventType type, std::uint32_t durationMs)
    {
        std::scoped_lock lock(_mutex);

        _currentFocus = type;
        _focusExpireTime = Clock::now() + std::chrono::milliseconds(durationMs);

        logger::info("[Haptics][Focus] Activated: type={} duration={}ms",
            static_cast<int>(type), durationMs);
    }

    void FocusManager::Update()
    {
        std::scoped_lock lock(_mutex);

        if (_currentFocus == EventType::Unknown) {
            return;
        }

        auto now = Clock::now();
        if (now >= _focusExpireTime) {
            logger::info("[Haptics][Focus] Expired: type={}", static_cast<int>(_currentFocus));
            _currentFocus = EventType::Unknown;
        }
    }

    bool FocusManager::HasFocus() const
    {
        std::scoped_lock lock(_mutex);
        return _currentFocus != EventType::Unknown;
    }

    EventType FocusManager::GetCurrentFocus() const
    {
        std::scoped_lock lock(_mutex);
        return _currentFocus;
    }

    std::uint32_t FocusManager::GetRemainingMs() const
    {
        std::scoped_lock lock(_mutex);

        if (_currentFocus == EventType::Unknown) {
            return 0;
        }

        auto now = Clock::now();
        if (now >= _focusExpireTime) {
            return 0;
        }

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            _focusExpireTime - now);
        return static_cast<std::uint32_t>(remaining.count());
    }

    float FocusManager::GetDuckFactorFor(EventType targetType) const
    {
        std::scoped_lock lock(_mutex);

        if (_currentFocus == EventType::Unknown) {
            return 1.0f;  // 无焦点，不压制
        }

        // 查找匹配的 ducking 规则
        for (const auto& rule : _duckingRules) {
            if (rule.focusType == _currentFocus && rule.targetType == targetType) {
                return rule.duckFactor;
            }
        }

        return 1.0f;  // 无规则，不压制
    }

    void FocusManager::SetDuckingRules(const std::vector<DuckingRule>& rules)
    {
        std::scoped_lock lock(_mutex);
        _duckingRules = rules;

        logger::info("[Haptics][Focus] Loaded {} ducking rules", rules.size());
    }
}