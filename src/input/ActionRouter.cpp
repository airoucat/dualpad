#include "pch.h"
#include "input/ActionRouter.h"

#include <string>

namespace
{
    using dualpad::input::ToString;
    using dualpad::input::TriggerCode;
    using dualpad::input::TriggerPhase;

    std::string BuildInputActionId(TriggerCode code, TriggerPhase phase)
    {
        std::string s("Input.");
        s += ToString(code);
        s += ".";
        s += ToString(phase);
        return s;
    }
}

namespace dualpad::input
{
    ActionRouter& ActionRouter::GetSingleton()
    {
        static ActionRouter inst;
        return inst;
    }

    void ActionRouter::InitDefaultBindings()
    {
        std::scoped_lock lk(_mtx);
        _buttonBindings.clear();
        _axisBindings.clear();

        // 最小默认映射（会被配置覆盖）
        _buttonBindings.emplace(Key{ TriggerCode::Cross, TriggerPhase::Press }, "Game.Confirm");
        _buttonBindings.emplace(Key{ TriggerCode::Circle, TriggerPhase::Press }, "Game.Back");
        _buttonBindings.emplace(Key{ TriggerCode::Triangle, TriggerPhase::Press }, "Game.OpenInventory");
        _buttonBindings.emplace(Key{ TriggerCode::Options, TriggerPhase::Press }, "Game.OpenTweenMenu");

        _axisBindings.emplace(AxisCode::LStickX, AxisBindingEntry{ AxisCode::LStickX, "Game.MoveX", 1.0f, false });
        _axisBindings.emplace(AxisCode::LStickY, AxisBindingEntry{ AxisCode::LStickY, "Game.MoveY", 1.0f, true });
        _axisBindings.emplace(AxisCode::RStickX, AxisBindingEntry{ AxisCode::RStickX, "Game.LookX", 1.0f, false });
        _axisBindings.emplace(AxisCode::RStickY, AxisBindingEntry{ AxisCode::RStickY, "Game.LookY", 1.0f, true });
        _axisBindings.emplace(AxisCode::L2, AxisBindingEntry{ AxisCode::L2, "Game.TriggerL", 1.0f, false });
        _axisBindings.emplace(AxisCode::R2, AxisBindingEntry{ AxisCode::R2, "Game.TriggerR", 1.0f, false });
    }

    void ActionRouter::ReplaceBindings(std::vector<BindingEntry> buttonBindings,
        std::vector<AxisBindingEntry> axisBindings)
    {
        std::scoped_lock lk(_mtx);

        _buttonBindings.clear();
        _axisBindings.clear();

        for (auto& b : buttonBindings) {
            if (b.code == TriggerCode::None || b.actionId.empty()) {
                continue;
            }
            _buttonBindings.emplace(Key{ b.code, b.phase }, std::move(b.actionId));
        }

        for (auto& a : axisBindings) {
            if (a.actionId.empty()) {
                continue;
            }
            _axisBindings.emplace(a.code, std::move(a));
        }
    }

    void ActionRouter::EmitInput(TriggerCode code, TriggerPhase phase)
    {
        if (code == TriggerCode::None) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lk(_mtx);

        // 1) raw Input.*
        _pending.push_back(ActionEvent{
            BuildInputActionId(code, phase),
            code,
            phase,
            now
            });

        // 2) mapped Game.*
        const Key k{ code, phase };
        auto [it, end] = _buttonBindings.equal_range(k);
        for (; it != end; ++it) {
            _pending.push_back(ActionEvent{ it->second, code, phase, now });
        }
    }

    void ActionRouter::EmitInputRaw(TriggerCode code, TriggerPhase phase)
    {
        if (code == TriggerCode::None) {
            return;
        }

        std::scoped_lock lk(_mtx);
        _pending.push_back(ActionEvent{
            BuildInputActionId(code, phase),
            code,
            phase,
            std::chrono::steady_clock::now()
            });
    }

    std::vector<ActionRouter::ActionEvent> ActionRouter::Drain()
    {
        std::scoped_lock lk(_mtx);
        std::vector<ActionEvent> out;
        out.swap(_pending);
        return out;
    }

    void ActionRouter::EmitAxis(AxisCode code, float value)
    {
        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lk(_mtx);

        auto [it, end] = _axisBindings.equal_range(code);
        for (; it != end; ++it) {
            const auto& b = it->second;
            float v = value * b.scale;
            if (b.invert) {
                v = -v;
            }
            _pendingAxis.push_back(AxisEvent{ b.actionId, v, now });
        }
    }

    void ActionRouter::EmitAxisAction(std::string_view actionId, float value)
    {
        if (actionId.empty()) {
            return;
        }

        std::scoped_lock lk(_mtx);
        _pendingAxis.push_back(AxisEvent{
            std::string(actionId),
            value,
            std::chrono::steady_clock::now()
            });
    }

    std::vector<ActionRouter::AxisEvent> ActionRouter::DrainAxis()
    {
        std::scoped_lock lk(_mtx);
        std::vector<AxisEvent> out;
        out.swap(_pendingAxis);
        return out;
    }
}