#pragma once

#include "input/InputActions.h"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dualpad::input
{
    class ActionRouter
    {
    public:
        struct ActionEvent
        {
            std::string actionId;
            TriggerCode code{ TriggerCode::None };
            TriggerPhase phase{ TriggerPhase::Press };
            std::chrono::steady_clock::time_point timestamp{};
        };

        struct AxisEvent
        {
            std::string actionId;
            float value{ 0.0f };
            std::chrono::steady_clock::time_point timestamp{};
        };

        struct BindingEntry
        {
            TriggerCode code{ TriggerCode::None };
            TriggerPhase phase{ TriggerPhase::Press };
            std::string actionId;
        };

        struct AxisBindingEntry
        {
            AxisCode code{ AxisCode::LStickX };
            std::string actionId;
            float scale{ 1.0f };
            bool invert{ false };
        };

        static ActionRouter& GetSingleton();

        void InitDefaultBindings();

        // button/trigger events
        void EmitInput(TriggerCode code, TriggerPhase phase);
        // raw only, no binding mapping (用于native submitter fallback，避免回环)
        void EmitInputRaw(TriggerCode code, TriggerPhase phase);
        std::vector<ActionEvent> Drain();

        // axis events
        void EmitAxis(AxisCode code, float value);
        void EmitAxisAction(std::string_view actionId, float value);
        std::vector<AxisEvent> DrainAxis();

        // config replace
        void ReplaceBindings(std::vector<BindingEntry> buttonBindings,
            std::vector<AxisBindingEntry> axisBindings);

    private:
        ActionRouter() = default;

        struct Key
        {
            TriggerCode code{ TriggerCode::None };
            TriggerPhase phase{ TriggerPhase::Press };

            bool operator==(const Key& rhs) const
            {
                return code == rhs.code && phase == rhs.phase;
            }
        };

        struct KeyHash
        {
            std::size_t operator()(const Key& k) const noexcept
            {
                return (static_cast<std::size_t>(k.code) << 8) ^
                    static_cast<std::size_t>(k.phase);
            }
        };

        std::mutex _mtx;

        std::unordered_multimap<Key, std::string, KeyHash> _buttonBindings;
        std::unordered_multimap<AxisCode, AxisBindingEntry> _axisBindings;

        std::vector<ActionEvent> _pending;
        std::vector<AxisEvent> _pendingAxis;
    };
}