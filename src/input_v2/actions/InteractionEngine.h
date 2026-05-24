#pragma once

#include "input_v2/actions/ActionManifest.h"
#include "input_v2/actions/ActionSetResolver.h"
#include "input_v2/actions/CompiledActionGraph.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dualpad::input_v2::actions
{
    enum class ActionPhase : std::uint8_t
    {
        Press = 0,
        Hold,
        Repeat,
        Release,
        Pulse,
        Value
    };

    struct ActionPhaseChange
    {
        ActionId actionId;
        BindingId bindingId{ 0 };
        ActionPhase phase{ ActionPhase::Press };
        std::uint64_t timestampUs{ 0 };
    };

    struct ActionValueSnapshot
    {
        ActionId actionId;
        ActionValueKind kind{ ActionValueKind::Unknown };
        float scalar{ 0.0f };
        float x{ 0.0f };
        float y{ 0.0f };
    };

    struct ActionOwnershipHint
    {
        ActionId actionId;
        BindingId bindingId{ 0 };
        std::string reason;
    };

    struct ResolvedActionFrame
    {
        std::uint64_t manifestEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::vector<ActionPhaseChange> changes;
        std::vector<ActionValueSnapshot> values;
        std::vector<ActionOwnershipHint> ownershipHints;
    };

    struct ControlSample
    {
        ControlPath path;
        bool down{ false };
        bool pressed{ false };
        bool released{ false };
        float scalar{ 0.0f };
        std::uint64_t downAtUs{ 0 };
        std::uint64_t timestampUs{ 0 };
    };

    struct InteractionInputFrame
    {
        std::uint64_t manifestEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t monotonicUs{ 0 };
        std::vector<ControlSample> samples;
    };

    struct InteractionBindingState
    {
        bool active{ false };
        bool holdFired{ false };
        bool toggleLatched{ false };
        bool chordLatched{ false };
        bool tapCandidate{ false };
        std::uint64_t pressedAtUs{ 0 };
        std::uint64_t lastRepeatAtUs{ 0 };
        float currentScalar{ 0.0f };
    };

    class InteractionStateStore
    {
    public:
        InteractionBindingState& ForBinding(BindingId bindingId);
        const InteractionBindingState* Find(BindingId bindingId) const;
        void Reset();

    private:
        std::unordered_map<BindingId, InteractionBindingState> _states;
    };

    class InteractionEngine
    {
    public:
        ResolvedActionFrame Resolve(
            const CompiledActionGraph& graph,
            const ActionSetStack& actionSetStack,
            const InteractionInputFrame& frame,
            InteractionStateStore& stateStore) const;

        static std::optional<ControlSample> FindSample(
            const InteractionInputFrame& frame,
            const ControlPath& path);

    private:
        static float ApplyModifiers(float value, const std::vector<BindingModifier>& modifiers);
    };
}
