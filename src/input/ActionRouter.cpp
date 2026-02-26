#include "pch.h"
#include "input/ActionRouter.h"
#include "input/GameActions.h"

#include <SKSE/SKSE.h>
#include <cmath>
#include <format>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::size_t AxisIndex(AxisCode a)
        {
            switch (a) {
            case AxisCode::LStickX: return 0;
            case AxisCode::LStickY: return 1;
            case AxisCode::RStickX: return 2;
            case AxisCode::RStickY: return 3;
            case AxisCode::L2:      return 4;
            case AxisCode::R2:      return 5;
            default:                return 0;
            }
        }

        constexpr bool IsKnownAxis(AxisCode a)
        {
            switch (a) {
            case AxisCode::LStickX:
            case AxisCode::LStickY:
            case AxisCode::RStickX:
            case AxisCode::RStickY:
            case AxisCode::L2:
            case AxisCode::R2:
                return true;
            default:
                return false;
            }
        }

        constexpr float AxisEpsilon(AxisCode a)
        {
            switch (a) {
            case AxisCode::LStickX:
            case AxisCode::LStickY:
            case AxisCode::RStickX:
            case AxisCode::RStickY:
                return 0.0035f;
            case AxisCode::L2:
            case AxisCode::R2:
                return 0.0060f;
            default:
                return 0.0040f;
            }
        }

        constexpr auto kAxisKeepAlive = std::chrono::milliseconds(100);
    }

    ActionRouter& ActionRouter::GetSingleton()
    {
        static ActionRouter s;
        return s;
    }

    std::string ActionRouter::BuildInputActionId(TriggerCode code, TriggerPhase phase) const
    {
        return std::format("Input.{}.{}", ToString(code), ToString(phase));
    }

    std::string ActionRouter::BuildInputAxisId(AxisCode axis) const
    {
        return std::format("InputAxis.{}", ToString(axis));
    }

    void ActionRouter::InitDefaultBindings()
    {
        std::scoped_lock lk(_mtx);
        _bindings.clear();
        _axisBindings.clear();

        _bindings[{ TriggerCode::TpLeftPress, TriggerPhase::Press }] =
            actions::ToString(actions::ToActionId(actions::ButtonAction::OpenInventory));
        _bindings[{ TriggerCode::TpRightPress, TriggerPhase::Press }] =
            actions::ToString(actions::ToActionId(actions::ButtonAction::OpenMagic));
        _bindings[{ TriggerCode::TpSwipeUp, TriggerPhase::Pulse }] =
            actions::ToString(actions::ToActionId(actions::ButtonAction::OpenMap));
        _bindings[{ TriggerCode::TpSwipeLeft, TriggerPhase::Pulse }] =
            actions::ToString(actions::ToActionId(actions::ButtonAction::OpenJournal));
        _axisBindings[AxisCode::LStickX] = actions::ToString(actions::ToActionId(actions::AxisAction::MoveX));
        _axisBindings[AxisCode::LStickY] = actions::ToString(actions::ToActionId(actions::AxisAction::MoveY));
        _axisBindings[AxisCode::RStickX] = actions::ToString(actions::ToActionId(actions::AxisAction::LookX));
        _axisBindings[AxisCode::RStickY] = actions::ToString(actions::ToActionId(actions::AxisAction::LookY));
        _axisBindings[AxisCode::L2] = actions::ToString(actions::ToActionId(actions::AxisAction::TriggerL));
        _axisBindings[AxisCode::R2] = actions::ToString(actions::ToActionId(actions::AxisAction::TriggerR));

        _axisLastValid.fill(false);
        _axisLastValue.fill(0.0f);
        _axisLastEmitAt.fill(std::chrono::steady_clock::time_point{});
        _axisUnboundWarned.fill(false);
    }

    void ActionRouter::EmitInput(TriggerCode code, TriggerPhase phase)
    {
        if (code == TriggerCode::None) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lk(_mtx);

        _pending.push_back(ActionEvent{ BuildInputActionId(code, phase), code, phase, now });

        const TriggerKey k{ code, phase };
        if (auto it = _bindings.find(k); it != _bindings.end()) {
            _pending.push_back(ActionEvent{ it->second, code, phase, now });
        }
    }

    void ActionRouter::ReplaceBindings(std::vector<BindingEntry> entries)
    {
        std::scoped_lock lk(_mtx);
        _bindings.clear();

        for (auto& e : entries) {
            if (e.code == TriggerCode::None || e.actionId.empty()) {
                continue;
            }
            _bindings[{ e.code, e.phase }] = std::move(e.actionId);
        }

        logger::info("[DualPad] Replaced bindings: {}", _bindings.size());
    }

    void ActionRouter::EmitAxis(AxisCode axis, float value)
    {
        if (!IsKnownAxis(axis)) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lk(_mtx);

        const auto idx = AxisIndex(axis);
        const auto eps = AxisEpsilon(axis);

        bool shouldEmit = false;
        if (!_axisLastValid[idx]) {
            shouldEmit = true;
        }
        else {
            const bool changed = std::fabs(value - _axisLastValue[idx]) >= eps;
            const bool keepAliveDue = (now - _axisLastEmitAt[idx]) >= kAxisKeepAlive;
            shouldEmit = changed || keepAliveDue;
        }

        if (!shouldEmit) {
            return;
        }

        _axisLastValid[idx] = true;
        _axisLastValue[idx] = value;
        _axisLastEmitAt[idx] = now;

        // raw axis observe channel
        _pendingAxis.push_back(AxisEvent{ BuildInputAxisId(axis), axis, value, now });

        // mapped game channel
        if (auto it = _axisBindings.find(axis); it != _axisBindings.end()) {
            _pendingAxis.push_back(AxisEvent{ it->second, axis, value, now });
        }
        else {
            if (!_axisUnboundWarned[idx]) {
                _axisUnboundWarned[idx] = true;
                logger::warn("[DualPad] Axis unbound: {}", ToString(axis));
            }
        }
    }

    void ActionRouter::ReplaceAxisBindings(std::vector<AxisBindingEntry> entries)
    {
        std::scoped_lock lk(_mtx);
        _axisBindings.clear();

        for (auto& e : entries) {
            if (!e.actionId.empty()) {
                _axisBindings[e.axis] = std::move(e.actionId);
            }
        }

        logger::info("[DualPad] Replaced axis bindings: {}", _axisBindings.size());
        for (auto& [axis, action] : _axisBindings) {
            logger::info("[DualPad]   axis {} -> {}", ToString(axis), action);
        }

        _axisLastValid.fill(false);
        _axisLastValue.fill(0.0f);
        _axisLastEmitAt.fill(std::chrono::steady_clock::time_point{});
        _axisUnboundWarned.fill(false);
    }

    std::vector<AxisEvent> ActionRouter::DrainAxis()
    {
        std::vector<AxisEvent> out;
        std::scoped_lock lk(_mtx);
        out.swap(_pendingAxis);
        return out;
    }

    std::vector<ActionEvent> ActionRouter::Drain()
    {
        std::vector<ActionEvent> out;
        std::scoped_lock lk(_mtx);
        out.swap(_pending);
        return out;
    }
}