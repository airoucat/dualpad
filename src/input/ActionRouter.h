#pragma once
#include "input/InputActions.h"

#include <chrono>
#include <mutex>
#include <array>
#include <string>
#include <vector>
#include <unordered_map>

namespace dualpad::input
{
    struct ActionEvent
    {
        std::string actionId;  // e.g. Input.Square.Press / Game.OpenInventory
        TriggerCode code{ TriggerCode::None };
        TriggerPhase phase{ TriggerPhase::Press };
        std::chrono::steady_clock::time_point when{};
    };

    struct AxisEvent
    {
        std::string actionId;  // e.g. InputAxis.LStickX / Game.MoveX
        AxisCode axis{ AxisCode::LStickX };
        float value{ 0.0f };
        std::chrono::steady_clock::time_point when{};
    };

    class ActionRouter
    {
    public:
        static ActionRouter& GetSingleton();

        void InitDefaultBindings();

        // button/trigger events
        void EmitInput(TriggerCode code, TriggerPhase phase);
        std::vector<ActionEvent> Drain();

        struct BindingEntry
        {
            TriggerCode code{ TriggerCode::None };
            TriggerPhase phase{ TriggerPhase::Press };
            std::string actionId;
        };
        void ReplaceBindings(std::vector<BindingEntry> entries);

        // axis events
        struct AxisBindingEntry
        {
            AxisCode axis{ AxisCode::LStickX };
            std::string actionId;
        };

        void EmitAxis(AxisCode axis, float value);
        void ReplaceAxisBindings(std::vector<AxisBindingEntry> entries);
        std::vector<AxisEvent> DrainAxis();

    private:
        ActionRouter() = default;

        struct TriggerKey {
            TriggerCode code;
            TriggerPhase phase;
            bool operator==(const TriggerKey& r) const noexcept
            {
                return code == r.code && phase == r.phase;
            }
        };
        struct TriggerKeyHash {
            std::size_t operator()(const TriggerKey& k) const noexcept
            {
                return (static_cast<std::size_t>(k.code) << 8) ^ static_cast<std::size_t>(k.phase);
            }
        };

        std::string BuildInputActionId(TriggerCode code, TriggerPhase phase) const;
        std::string BuildInputAxisId(AxisCode axis) const;

        std::mutex _mtx;
        std::vector<ActionEvent> _pending;
        std::vector<AxisEvent> _pendingAxis;

        static constexpr std::size_t kAxisCount = 6;
        std::array<float, kAxisCount> _axisLastValue{};
        std::array<bool, kAxisCount> _axisLastValid{};
        std::array<std::chrono::steady_clock::time_point, kAxisCount> _axisLastEmitAt{};
        std::array<bool, kAxisCount> _axisUnboundWarned{};  // <- 新增：每轴只告警一次

        std::unordered_map<TriggerKey, std::string, TriggerKeyHash> _bindings;
        std::unordered_map<AxisCode, std::string> _axisBindings;
    };
}