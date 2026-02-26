#include "pch.h"
#include "input/GameActionOutput.h"
#include "input/ActionRouter.h"
#include "input/AxisDiag.h"
#include "input/GameActions.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <cmath>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef DUALPAD_TRACE_APPLY
#define DUALPAD_TRACE_APPLY 0
#endif

namespace logger = SKSE::log;

namespace
{
    struct AxisAccum
    {
        float moveX{ 0.0f };
        float moveY{ 0.0f };
        float lookX{ 0.0f };
        float lookY{ 0.0f };
        float l2{ 0.0f };
        float r2{ 0.0f };
    };

    std::mutex g_mtx;
    AxisAccum g_axisLatched{};                    // “最新轴状态” (latched)
    std::vector<std::string> g_pendingActions;    // 一次性 Game.* action

    std::thread::id g_boundThread{};
    std::atomic_bool g_warnWrongThreadOnce{ false };

    inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
    inline float Clamp11(float v) { return std::clamp(v, -1.0f, 1.0f); }

    inline std::size_t TidHash(std::thread::id tid)
    {
        return std::hash<std::thread::id>{}(tid);
    }

    std::atomic<std::uint64_t> g_nextApplyDiagMs{ 0 };
    std::atomic<std::uint64_t> g_nextPausedWarnMs{ 0 };

    inline std::uint64_t NowSteadyMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    inline bool TryRateLimit(std::atomic<std::uint64_t>& gate, std::uint64_t now, std::uint64_t intervalMs)
    {
        auto old = gate.load(std::memory_order_relaxed);
        if (now < old) {
            return false;
        }
        return gate.compare_exchange_strong(old, now + intervalMs, std::memory_order_relaxed);
    }

    void ShowMenuIfClosed(std::string_view menuName)
    {
        auto* ui = RE::UI::GetSingleton();
        auto* q = RE::UIMessageQueue::GetSingleton();
        if (!ui || !q || menuName.empty()) {
            return;
        }

        RE::BSFixedString menuFixed(menuName.data());
        if (ui->IsMenuOpen(menuFixed)) {
            return;
        }

        q->AddMessage(menuFixed, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    }

    void ExecuteGameActionNow(std::string_view actionId)
    {
        using BA = dualpad::input::actions::ButtonAction;
        switch (dualpad::input::actions::ParseButtonAction(actionId)) {
        case BA::OpenInventory:
            ShowMenuIfClosed(RE::InventoryMenu::MENU_NAME);
            break;
        case BA::OpenMagic:
            ShowMenuIfClosed(RE::MagicMenu::MENU_NAME);
            break;
        case BA::OpenMap:
            ShowMenuIfClosed(RE::MapMenu::MENU_NAME);
            break;
        case BA::OpenJournal:
            ShowMenuIfClosed(RE::JournalMenu::MENU_NAME);
            break;
        case BA::Unknown:
        default:
            // 非按钮 Game 动作在这里忽略（比如 axis）
            break;
        }
    }

    void LatchAxis(std::string_view actionId, float value, AxisAccum& axis)
    {
        using AA = dualpad::input::actions::AxisAction;
        switch (dualpad::input::actions::ParseAxisAction(actionId)) {
        case AA::MoveX:    axis.moveX = Clamp11(value); break;
        case AA::MoveY:    axis.moveY = Clamp11(value); break;
        case AA::LookX:    axis.lookX = Clamp11(value); break;
        case AA::LookY:    axis.lookY = Clamp11(value); break;
        case AA::TriggerL: axis.l2 = Clamp01(value); break;
        case AA::TriggerR: axis.r2 = Clamp01(value); break;
        case AA::Unknown:
        default:
            logger::warn("[DualPad] Unknown Game axis action: {}", actionId);
            break;
        }
        dualpad::diag::dispatchPerSec.fetch_add(1, std::memory_order_relaxed);
    }

    void ApplyAxisToGame(const AxisAccum& axis)
    {
        auto* pc = RE::PlayerControls::GetSingleton();
        if (!pc) {
            logger::warn("[DualPad] ApplyAxisToGame: PlayerControls is null");
            return;
        }
#if DUALPAD_TRACE_APPLY
        // 写入前快照
        const float beforeMX = pc->data.moveInputVec.x;
        const float beforeMY = pc->data.moveInputVec.y;
        const float beforeLX = pc->data.lookInputVec.x;
        const float beforeLY = pc->data.lookInputVec.y;
#endif

        // 实际写入
        pc->data.moveInputVec.x = axis.moveX;
        pc->data.moveInputVec.y = axis.moveY;
        pc->data.lookInputVec.x = axis.lookX;
        pc->data.lookInputVec.y = axis.lookY;

#if DUALPAD_TRACE_APPLY
        // 写入后快照
        const float afterMX = pc->data.moveInputVec.x;
        const float afterMY = pc->data.moveInputVec.y;
        const float afterLX = pc->data.lookInputVec.x;
        const float afterLY = pc->data.lookInputVec.y;
#endif

        dualpad::diag::applyPerSec.fetch_add(1, std::memory_order_relaxed);
        dualpad::diag::lastWriteMoveX.store(axis.moveX, std::memory_order_relaxed);
        dualpad::diag::lastWriteMoveY.store(axis.moveY, std::memory_order_relaxed);
        dualpad::diag::lastWriteMs.store(dualpad::diag::NowMs(), std::memory_order_relaxed);

        // 诊断：每秒打一条
        const auto now = NowSteadyMs();
        const auto* ui = RE::UI::GetSingleton();
        const bool paused = ui ? const_cast<RE::UI*>(ui)->GameIsPaused() : false;
#if DUALPAD_TRACE_APPLY
        if (TryRateLimit(g_nextApplyDiagMs, now, 1000)) {
            logger::info(
                "[DualPad][ApplyDiag] tid={} paused={} "
                "axis(mv={:.3f},{:.3f} look={:.3f},{:.3f}) "
                "before(mv={:.3f},{:.3f} look={:.3f},{:.3f}) "
                "after(mv={:.3f},{:.3f} look={:.3f},{:.3f})",
                TidHash(std::this_thread::get_id()),
                paused ? 1 : 0,
                axis.moveX, axis.moveY, axis.lookX, axis.lookY,
                beforeMX, beforeMY, beforeLX, beforeLY,
                afterMX, afterMY, afterLX, afterLY
            );
        }
#endif
        // 诊断：有输入但 paused（每2秒警告一次）
        constexpr float kEps = 0.02f;
        const bool hasMove = std::fabs(axis.moveX) > kEps || std::fabs(axis.moveY) > kEps;
        const bool hasLook = std::fabs(axis.lookX) > kEps || std::fabs(axis.lookY) > kEps;
        if ((hasMove || hasLook) && paused && TryRateLimit(g_nextPausedWarnMs, now, 2000)) {
            logger::warn("[DualPad][ApplyDiag] non-zero axis while game paused");
        }
    }
}

namespace dualpad::input
{
    void BindGameOutputThread()
    {
        g_boundThread = std::this_thread::get_id();
        g_warnWrongThreadOnce.store(false, std::memory_order_relaxed);

        logger::info(
            "[DualPad] GameOutput bound thread hash={}",
            TidHash(g_boundThread));
    }

    void CollectGameOutputCommands()
    {
        // 1) 收按钮动作
        auto actions = ActionRouter::GetSingleton().Drain();
        // 2) 收轴动作
        auto axes = ActionRouter::GetSingleton().DrainAxis();

        std::scoped_lock lk(g_mtx);

        for (auto& e : actions) {
            if (e.actionId.rfind("Game.", 0) == 0) {
                g_pendingActions.emplace_back(e.actionId);
            }
        }

        for (auto& e : axes) {
            dualpad::diag::axisInPerSec.fetch_add(1, std::memory_order_relaxed);

            if (e.actionId.rfind("Game.", 0) == 0) {
                LatchAxis(e.actionId, e.value, g_axisLatched);
            }
        }
    }

    void FlushGameOutputOnBoundThread()
    {
        AxisAccum axisCopy{};
        std::vector<std::string> actionsCopy;

        {
            std::scoped_lock lk(g_mtx);
            axisCopy = g_axisLatched;
            actionsCopy.swap(g_pendingActions);
        }

        for (auto& a : actionsCopy) {
            ExecuteGameActionNow(a);
        }

        ApplyAxisToGame(axisCopy);
    }
}