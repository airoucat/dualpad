#include "pch.h"
#include "input/GameActionOutput.h"

#include "input/ActionRouter.h"
#include "input/GameActions.h"
#include "input/ActionExecutor.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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

    struct PendingGameAction
    {
        std::string actionId;
        dualpad::input::TriggerPhase phase{ dualpad::input::TriggerPhase::Press };
    };

    std::mutex g_mtx;
    AxisAccum g_axisLatched{};
    std::vector<PendingGameAction> g_pendingActions;

    inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
    inline float Clamp11(float v) { return std::clamp(v, -1.0f, 1.0f); }

    std::string_view StripGamePrefix(std::string_view s)
    {
        if (s.rfind("Game.", 0) == 0) {
            s.remove_prefix(5);
        }
        return s;
    }

    void WarnUnhandledOnce(std::string_view actionId)
    {
        static std::mutex s_mtx;
        static std::unordered_set<std::string> s_warned;

        std::scoped_lock lk(s_mtx);
        auto [it, inserted] = s_warned.emplace(actionId.data(), actionId.size());
        if (inserted) {
            logger::warn("[DualPad] Unhandled Game action: {}", *it);
        }
    }

    void WarnUnknownAxisOnce(std::string_view actionId)
    {
        static std::mutex s_mtx;
        static std::unordered_set<std::string> s_warned;

        std::scoped_lock lk(s_mtx);
        auto [it, inserted] = s_warned.emplace(actionId.data(), actionId.size());
        if (inserted) {
            logger::warn("[DualPad] Unknown axis action id: {}", *it);
        }
    }

    dualpad::input::actions::AxisAction ParseAxisCompat(std::string_view actionId)
    {
        using dualpad::input::actions::AxisAction;
        auto a = dualpad::input::actions::ParseAxisAction(actionId);
        if (a != AxisAction::Unknown) {
            return a;
        }
        return dualpad::input::actions::ParseAxisAction(StripGamePrefix(actionId));
    }

    void LatchAxis(std::string_view actionId, float value, AxisAccum& axis)
    {
        using AA = dualpad::input::actions::AxisAction;
        switch (ParseAxisCompat(actionId)) {
        case AA::MoveX:    axis.moveX = Clamp11(value); break;
        case AA::MoveY:    axis.moveY = Clamp11(value); break;
        case AA::LookX:    axis.lookX = Clamp11(value); break;
        case AA::LookY:    axis.lookY = Clamp11(value); break;
        case AA::TriggerL: axis.l2 = Clamp01(value); break;
        case AA::TriggerR: axis.r2 = Clamp01(value); break;
        default:
            WarnUnknownAxisOnce(actionId);
            break;
        }
    }

    void ApplyAxisToGame(const AxisAccum& axis)
    {
        auto* pc = RE::PlayerControls::GetSingleton();
        if (!pc) {
            return;
        }

        pc->data.moveInputVec.x = axis.moveX;
        pc->data.moveInputVec.y = axis.moveY;
        pc->data.lookInputVec.x = axis.lookX;
        pc->data.lookInputVec.y = axis.lookY;
    }
}

namespace dualpad::input
{
    void BindGameOutputThread()
    {
        logger::info("[DualPad] GameOutput ready");
    }

    void CollectGameOutputCommands()
    {
        auto actions = ActionRouter::GetSingleton().Drain();
        auto axes = ActionRouter::GetSingleton().DrainAxis();

        std::scoped_lock lk(g_mtx);

        for (auto& e : actions) {
            if (e.actionId.rfind("Game.", 0) == 0) {
                g_pendingActions.push_back(PendingGameAction{ e.actionId, e.phase });
            }
        }

        for (auto& e : axes) {
            if (e.actionId.rfind("Game.", 0) == 0) {
                LatchAxis(e.actionId, e.value, g_axisLatched);
            }
        }
    }

    void FlushGameOutputOnBoundThread()
    {
        // ∑¿÷ÿ»Î
        static thread_local bool s_inFlush = false;
        if (s_inFlush) {
            return;
        }

        s_inFlush = true;

        AxisAccum axisCopy{};
        std::vector<PendingGameAction> actionsCopy;

        {
            std::scoped_lock lk(g_mtx);
            axisCopy = g_axisLatched;
            actionsCopy.swap(g_pendingActions);
        }

        auto& exec = GetCompositeGameExecutor();
        for (auto& a : actionsCopy) {
            if (!exec.ExecuteButton(a.actionId, a.phase)) {
                WarnUnhandledOnce(a.actionId);
            }
        }

        ApplyAxisToGame(axisCopy);

        s_inFlush = false;
    }
}