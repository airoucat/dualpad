#include "pch.h"
#include "input/ActionRouter.h"
#include <SKSE/SKSE.h>
#include <format>

namespace logger = SKSE::log;

namespace dualpad::input
{
    ActionRouter& ActionRouter::GetSingleton()
    {
        static ActionRouter s;
        return s;
    }

    std::string ActionRouter::BuildInputActionId(TriggerCode code, TriggerPhase phase) const
    {
        return std::format("Input.{}.{}", ToString(code), ToString(phase));
    }

    void ActionRouter::InitDefaultBindings()
    {
        std::scoped_lock lk(_mtx);
        _bindings.clear();

        // 最小 action 绑定（你后续可改成 json）
        _bindings[{ TriggerCode::TpLeftPress, TriggerPhase::Press }] = "Game.OpenInventory";
        _bindings[{ TriggerCode::TpRightPress, TriggerPhase::Press }] = "Game.OpenMagic";
        _bindings[{ TriggerCode::TpSwipeUp, TriggerPhase::Pulse }] = "Game.OpenMap";
        _bindings[{ TriggerCode::TpSwipeLeft, TriggerPhase::Pulse }] = "Game.OpenJournal";
    }

    void ActionRouter::EmitInput(TriggerCode code, TriggerPhase phase)
    {
        if (code == TriggerCode::None) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lk(_mtx);

        // 1) 所有输入都先变成 Input.* action（你要的“全记录 action 化”）
        _pending.push_back(ActionEvent{
            BuildInputActionId(code, phase), code, phase, now
            });

        logger::info("[DualPad] Action {}", _pending.back().actionId);

        // 2) 再看是否有 gameplay 绑定
        const TriggerKey k{ code, phase };
        if (auto it = _bindings.find(k); it != _bindings.end()) {
            _pending.push_back(ActionEvent{
                it->second, code, phase, now
                });
            logger::info("[DualPad] Action {}", _pending.back().actionId);
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

    std::vector<ActionEvent> ActionRouter::Drain()
    {
        std::vector<ActionEvent> out;
        std::scoped_lock lk(_mtx);
        out.swap(_pending);
        return out;
    }
}