#include "pch.h"
#include "haptics/FocusManager.h"
#include <SKSE/SKSE.h>
#include <algorithm>
#include <unordered_map>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        struct FocusDedupState
        {
            std::mutex mtx;
            std::unordered_map<int, std::int64_t> lastActivateUsByType;
        };

        FocusDedupState g_focusDedup;

        inline std::int64_t DedupWindowUs(EventType type)
        {
            switch (type) {
            case EventType::HitImpact:   return 80'000; // 80ms
            case EventType::Block:       return 60'000;
            case EventType::WeaponSwing: return 40'000;
            case EventType::Footstep:    return 25'000;
            default:                     return 35'000;
            }
        }

        inline bool PassFocusDedup(EventType type, std::int64_t nowUs)
        {
            const int key = static_cast<int>(type);
            const auto winUs = DedupWindowUs(type);

            std::scoped_lock lk(g_focusDedup.mtx);
            auto it = g_focusDedup.lastActivateUsByType.find(key);
            if (it != g_focusDedup.lastActivateUsByType.end()) {
                if ((nowUs - it->second) < winUs) {
                    return false;
                }
            }
            g_focusDedup.lastActivateUsByType[key] = nowUs;
            return true;
        }
    }

    void FocusManager::SetFocus(EventType type, std::uint32_t durationMs)
    {
        std::scoped_lock lock(_mutex);

        const auto now = Clock::now();

        // 1) 同类型已在焦点中：避免重复刷日志/重复激活
        if (_currentFocus == type && now < _focusExpireTime) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                _focusExpireTime - now).count();

            // 新请求时长没有明显增加就忽略
            if (durationMs <= static_cast<std::uint32_t>(std::max<std::int64_t>(0, remaining + 10))) {
                return;
            }

            // 新请求更长则仅延长，不重复“Activated”刷屏
            _focusExpireTime = now + std::chrono::milliseconds(durationMs);
            logger::info("[Haptics][Focus] Extended: type={} duration={}ms",
                static_cast<int>(type), durationMs);
            return;
        }

        // 2) 去重窗口：同类型短窗内重复激活直接忽略
        const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        if (!PassFocusDedup(type, nowUs)) {
            return;
        }

        _currentFocus = type;
        _focusExpireTime = now + std::chrono::milliseconds(durationMs);

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
            return 1.0f;
        }

        for (const auto& rule : _duckingRules) {
            if (rule.focusType == _currentFocus && rule.targetType == targetType) {
                return rule.duckFactor;
            }
        }

        return 1.0f;
    }

    void FocusManager::SetDuckingRules(const std::vector<DuckingRule>& rules)
    {
        std::scoped_lock lock(_mutex);
        _duckingRules = rules;

        logger::info("[Haptics][Focus] Loaded {} ducking rules", rules.size());
    }
}