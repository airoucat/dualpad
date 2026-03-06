#include "pch.h"
#include "input/InputContext.h"
#include "input/RaceTypeCache.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace logger = SKSE::log;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr auto kMenuLogMinInterval = std::chrono::milliseconds(500);
    constexpr auto kSuppressedSummaryInterval = std::chrono::seconds(3);
    constexpr float kPlayerMoveDelta2DPerFrameSq = 0.25f;

    struct MenuLogThrottleState
    {
        std::mutex mutex;
        std::unordered_map<std::string, Clock::time_point> lastLogByKey;
        std::uint64_t suppressedCount{ 0 };
        Clock::time_point lastSummaryTime{};
    };

    MenuLogThrottleState g_menuLogThrottle{};

    std::uint64_t NowSteadyUs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now().time_since_epoch())
                .count());
    }

    bool ShouldLogMenuEvent(std::string_view eventTag, std::string_view menuName)
    {
        const auto now = Clock::now();
        const std::string key = std::string(eventTag) + "|" + std::string(menuName);

        std::scoped_lock lock(g_menuLogThrottle.mutex);

        auto it = g_menuLogThrottle.lastLogByKey.find(key);
        if (it == g_menuLogThrottle.lastLogByKey.end()) {
            g_menuLogThrottle.lastLogByKey.emplace(key, now);
            return true;
        }

        if (now - it->second >= kMenuLogMinInterval) {
            it->second = now;
            return true;
        }

        ++g_menuLogThrottle.suppressedCount;
        return false;
    }

    void MaybeLogSuppressedSummary()
    {
        const auto now = Clock::now();

        std::uint64_t suppressedToPrint = 0;
        {
            std::scoped_lock lock(g_menuLogThrottle.mutex);

            if (g_menuLogThrottle.suppressedCount == 0) {
                return;
            }

            if (g_menuLogThrottle.lastSummaryTime.time_since_epoch().count() != 0 &&
                now - g_menuLogThrottle.lastSummaryTime < kSuppressedSummaryInterval) {
                return;
            }

            suppressedToPrint = g_menuLogThrottle.suppressedCount;
            g_menuLogThrottle.suppressedCount = 0;
            g_menuLogThrottle.lastSummaryTime = now;
        }

        logger::info("[DualPad][Context] Menu logs suppressed={} (throttled)", suppressedToPrint);
    }
}

namespace dualpad::input
{
    ContextManager& ContextManager::GetSingleton()
    {
        static ContextManager instance;
        return instance;
    }

    InputContext ContextManager::GetCurrentContext() const
    {
        return static_cast<InputContext>(_currentContextValue.load(std::memory_order_relaxed));
    }

    void ContextManager::SyncCurrentContext(InputContext context)
    {
        _currentContext = context;
        _currentContextValue.store(static_cast<std::uint16_t>(context), std::memory_order_relaxed);
    }

    InputContext ContextManager::MenuNameToContext(std::string_view menuName) const
    {
        if (menuName == RE::InventoryMenu::MENU_NAME) return InputContext::InventoryMenu;
        if (menuName == RE::MagicMenu::MENU_NAME) return InputContext::MagicMenu;
        if (menuName == RE::MapMenu::MENU_NAME) return InputContext::MapMenu;
        if (menuName == RE::JournalMenu::MENU_NAME) return InputContext::JournalMenu;

        if (menuName == "DialogueMenu") return InputContext::DialogueMenu;
        if (menuName == "FavoritesMenu") return InputContext::FavoritesMenu;
        if (menuName == "TweenMenu") return InputContext::TweenMenu;
        if (menuName == "ContainerMenu") return InputContext::ContainerMenu;
        if (menuName == "BarterMenu") return InputContext::BarterMenu;
        if (menuName == "Training Menu") return InputContext::TrainingMenu;
        if (menuName == "LevelUp Menu") return InputContext::LevelUpMenu;
        if (menuName == "RaceSex Menu") return InputContext::RaceSexMenu;
        if (menuName == "StatsMenu") return InputContext::StatsMenu;
        if (menuName == "SkillMenu") return InputContext::SkillMenu;
        if (menuName == "Book Menu") return InputContext::BookMenu;
        if (menuName == "MessageBoxMenu") return InputContext::MessageBoxMenu;
        if (menuName == "QuantityMenu") return InputContext::QuantityMenu;
        if (menuName == "GiftMenu") return InputContext::GiftMenu;
        if (menuName == "Creations Menu") return InputContext::CreationsMenu;

        if (menuName == "Console") return InputContext::Console;
        if (menuName == "Lockpicking Menu") return InputContext::Lockpicking;
        if (menuName == "Loading Menu") return InputContext::Menu;

        return InputContext::Menu;
    }

    InputContext ContextManager::DetectGameplayContext() const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return InputContext::Gameplay;
        }

        if (player->IsDead()) {
            return InputContext::Death;
        }

        if (player->AsActorValueOwner()) {
            const auto health = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            if (health <= 0.0f && !player->IsDead()) {
                return InputContext::Bleedout;
            }
        }

        if (player->IsInRagdollState()) {
            return InputContext::Ragdoll;
        }

        if (player->IsInKillMove()) {
            return InputContext::KillMove;
        }

        if (auto* race = player->GetRace()) {
            const auto raceType = RaceTypeCache::GetSingleton().Resolve(race->GetFormID());
            if (raceType == RaceType::Werewolf) {
                return InputContext::Werewolf;
            }
            if (raceType == RaceType::VampireLord) {
                return InputContext::VampireLord;
            }
        }

        if (player->IsOnMount()) {
            return InputContext::Riding;
        }

        if (player->IsSneaking()) {
            return InputContext::Sneaking;
        }

        return InputContext::Gameplay;
    }

    void ContextManager::UpdatePlayerMotionSnapshot()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            _playerMoving.store(false, std::memory_order_relaxed);
            _lastPlayerMoveUs.store(0, std::memory_order_relaxed);
            _hasLastPlayerPos = false;
            return;
        }

        const auto pos = player->GetPosition();
        if (!_hasLastPlayerPos) {
            _lastPlayerPosX = pos.x;
            _lastPlayerPosY = pos.y;
            _hasLastPlayerPos = true;
            _playerMoving.store(false, std::memory_order_relaxed);
            return;
        }

        const auto dx = pos.x - _lastPlayerPosX;
        const auto dy = pos.y - _lastPlayerPosY;
        const auto deltaSq = (dx * dx) + (dy * dy);
        const bool moving = deltaSq >= kPlayerMoveDelta2DPerFrameSq;
        _playerMoving.store(moving, std::memory_order_relaxed);
        if (moving) {
            _lastPlayerMoveUs.store(NowSteadyUs(), std::memory_order_relaxed);
        }

        _lastPlayerPosX = pos.x;
        _lastPlayerPosY = pos.y;
    }

    void ContextManager::UpdateGameplayContext()
    {
        const auto ctxValue = static_cast<std::uint16_t>(GetCurrentContext());
        if ((ctxValue >= 100 && ctxValue < 2000) || ctxValue == 200) {
            MaybeLogSuppressedSummary();
            return;
        }

        UpdatePlayerMotionSnapshot();

        const auto newContext = DetectGameplayContext();
        if (newContext != _currentContext) {
            logger::trace("[DualPad][Context] Gameplay context changed: {} -> {}",
                ToString(_currentContext), ToString(newContext));
            SyncCurrentContext(newContext);
        }

        MaybeLogSuppressedSummary();
    }

    void ContextManager::OnMenuOpen(std::string_view menuName)
    {
        const auto newContext = MenuNameToContext(menuName);
        _contextStack.push_back(_currentContext);
        SyncCurrentContext(newContext);
        _playerMoving.store(false, std::memory_order_relaxed);
        _lastPlayerMoveUs.store(0, std::memory_order_relaxed);
        _hasLastPlayerPos = false;

        if (ShouldLogMenuEvent("open", menuName)) {
            logger::info("[DualPad][Context] Menu opened: {} -> Context: {}",
                menuName, ToString(newContext));
        } else {
            MaybeLogSuppressedSummary();
        }
    }

    void ContextManager::OnMenuClose(std::string_view menuName)
    {
        if (!_contextStack.empty()) {
            SyncCurrentContext(_contextStack.back());
            _contextStack.pop_back();
        } else {
            SyncCurrentContext(DetectGameplayContext());
        }
        _playerMoving.store(false, std::memory_order_relaxed);
        _lastPlayerMoveUs.store(0, std::memory_order_relaxed);
        _hasLastPlayerPos = false;

        if (ShouldLogMenuEvent("close", menuName)) {
            logger::info("[DualPad][Context] Menu closed: {} -> Restored: {}",
                menuName, ToString(_currentContext));
        } else {
            MaybeLogSuppressedSummary();
        }
    }

    void ContextManager::PushContext(InputContext context)
    {
        _contextStack.push_back(_currentContext);
        SyncCurrentContext(context);
        logger::info("[DualPad][Context] Context pushed: {}", ToString(context));
    }

    void ContextManager::PopContext()
    {
        if (!_contextStack.empty()) {
            SyncCurrentContext(_contextStack.back());
            _contextStack.pop_back();
            logger::info("[DualPad][Context] Context popped to: {}", ToString(_currentContext));
        }
    }

    void ContextManager::SetContext(InputContext context)
    {
        if (_currentContext != context) {
            logger::info("[DualPad][Context] Context set: {} -> {}",
                ToString(_currentContext), ToString(context));
            SyncCurrentContext(context);
        }
    }

    bool ContextManager::IsFootstepContextAllowed() const
    {
        switch (GetCurrentContext()) {
        case InputContext::Gameplay:
        case InputContext::Combat:
        case InputContext::Sneaking:
        case InputContext::Werewolf:
        case InputContext::VampireLord:
            return true;
        default:
            return false;
        }
    }

    bool ContextManager::IsPlayerMoving() const
    {
        return _playerMoving.load(std::memory_order_relaxed);
    }

    bool ContextManager::WasPlayerMovingRecently(std::uint64_t recentWindowUs) const
    {
        if (IsPlayerMoving()) {
            return true;
        }

        const auto lastMoveUs = _lastPlayerMoveUs.load(std::memory_order_relaxed);
        if (lastMoveUs == 0 || recentWindowUs == 0) {
            return false;
        }

        const auto nowUs = NowSteadyUs();
        if (nowUs < lastMoveUs) {
            return false;
        }
        return (nowUs - lastMoveUs) <= recentWindowUs;
    }

    std::uint64_t ContextManager::GetLastPlayerMoveUs() const
    {
        return _lastPlayerMoveUs.load(std::memory_order_relaxed);
    }
}
