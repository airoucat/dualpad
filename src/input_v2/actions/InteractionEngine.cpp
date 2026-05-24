#include "pch.h"

#include "input_v2/actions/InteractionEngine.h"

#include <algorithm>
#include <cmath>

namespace dualpad::input_v2::actions
{
    namespace
    {
        void Emit(
            ResolvedActionFrame& resolved,
            const CompiledGraphBinding& binding,
            ActionPhase phase,
            std::uint64_t timestampUs)
        {
            resolved.changes.push_back(ActionPhaseChange{
                .actionId = binding.actionId,
                .bindingId = binding.bindingId,
                .phase = phase,
                .timestampUs = timestampUs
            });
        }

        bool RequiredPathsDown(
            const CompiledGraphBinding& binding,
            const InteractionInputFrame& frame)
        {
            for (const auto requiredIndex : binding.interaction.requiredPathIndices) {
                if (requiredIndex >= binding.paths.size()) {
                    return false;
                }
                const auto sample = InteractionEngine::FindSample(frame, binding.paths[requiredIndex]);
                if (!sample.has_value() || !sample->down) {
                    return false;
                }
            }
            return true;
        }

        bool AnyRequiredPressed(
            const CompiledGraphBinding& binding,
            const InteractionInputFrame& frame,
            std::uint64_t& pressedAtUs)
        {
            for (const auto requiredIndex : binding.interaction.requiredPathIndices) {
                if (requiredIndex >= binding.paths.size()) {
                    return false;
                }
                const auto sample = InteractionEngine::FindSample(frame, binding.paths[requiredIndex]);
                if (sample.has_value() && sample->pressed) {
                    pressedAtUs = sample->downAtUs != 0 ? sample->downAtUs : sample->timestampUs;
                    return true;
                }
            }
            return false;
        }

        bool ChordWindowSatisfied(
            const CompiledGraphBinding& binding,
            const InteractionInputFrame& frame,
            const ControlSample& primary)
        {
            for (const auto requiredIndex : binding.interaction.requiredPathIndices) {
                const auto required = InteractionEngine::FindSample(frame, binding.paths[requiredIndex]);
                if (!required.has_value() || !required->down) {
                    return false;
                }
                const auto primaryAt = primary.downAtUs != 0 ? primary.downAtUs : primary.timestampUs;
                const auto requiredAt = required->downAtUs != 0 ? required->downAtUs : required->timestampUs;
                const auto delta = primaryAt > requiredAt ? primaryAt - requiredAt : requiredAt - primaryAt;
                if (delta > binding.interaction.chordWindowUs) {
                    return false;
                }
            }
            return true;
        }
    }

    InteractionBindingState& InteractionStateStore::ForBinding(BindingId bindingId)
    {
        return _states[bindingId];
    }

    const InteractionBindingState* InteractionStateStore::Find(BindingId bindingId) const
    {
        const auto it = _states.find(bindingId);
        return it == _states.end() ? nullptr : &it->second;
    }

    void InteractionStateStore::Reset()
    {
        _states.clear();
    }

    ResolvedActionFrame InteractionEngine::Resolve(
        const CompiledActionGraph& graph,
        const ActionSetStack& actionSetStack,
        const InteractionInputFrame& frame,
        InteractionStateStore& stateStore) const
    {
        ResolvedActionFrame resolved{};
        resolved.manifestEpoch = frame.manifestEpoch;
        resolved.contextRevision = frame.contextRevision;

        if (frame.manifestEpoch != graph.manifestEpoch) {
            return resolved;
        }

        const auto visibleBindings = graph.BindingsForActionSet(actionSetStack.baseSetId, actionSetStack.layerIds);
        for (const auto* bindingPtr : visibleBindings) {
            const auto& binding = *bindingPtr;
            if (binding.interaction.primaryPathIndex >= binding.paths.size()) {
                continue;
            }

            const auto primary = FindSample(frame, binding.paths[binding.interaction.primaryPathIndex]);
            if (!primary.has_value()) {
                continue;
            }

            auto& state = stateStore.ForBinding(binding.bindingId);
            const auto requiredDown = RequiredPathsDown(binding, frame);
            const auto activeByPrimary = primary->down && requiredDown;
            const auto now = frame.monotonicUs != 0 ? frame.monotonicUs : primary->timestampUs;

            switch (binding.interaction.kind) {
            case InteractionKind::Value: {
                auto value = ApplyModifiers(primary->scalar, binding.modifiers);
                if (std::fabs(value - state.currentScalar) > 0.0001f || primary->pressed || primary->released) {
                    state.currentScalar = value;
                    resolved.values.push_back(ActionValueSnapshot{
                        .actionId = binding.actionId,
                        .kind = ActionValueKind::Axis1D,
                        .scalar = value,
                        .x = value,
                        .y = 0.0f
                    });
                    Emit(resolved, binding, ActionPhase::Value, now);
                }
                break;
            }
            case InteractionKind::Press:
            case InteractionKind::Gesture:
                if (primary->pressed && requiredDown && !state.active) {
                    state.active = true;
                    state.pressedAtUs = primary->downAtUs != 0 ? primary->downAtUs : now;
                    Emit(resolved, binding, ActionPhase::Press, now);
                }
                if (state.active && (!primary->down || primary->released || !requiredDown)) {
                    state.active = false;
                    Emit(resolved, binding, ActionPhase::Release, now);
                }
                break;
            case InteractionKind::Hold:
                if (primary->pressed && requiredDown) {
                    state.active = true;
                    state.holdFired = false;
                    state.pressedAtUs = primary->downAtUs != 0 ? primary->downAtUs : now;
                }
                if (state.active && activeByPrimary && !state.holdFired &&
                    now >= state.pressedAtUs + binding.interaction.holdThresholdUs) {
                    state.holdFired = true;
                    Emit(resolved, binding, ActionPhase::Hold, now);
                }
                if (state.active && (!primary->down || primary->released || !requiredDown)) {
                    if (state.holdFired) {
                        Emit(resolved, binding, ActionPhase::Release, now);
                    }
                    state = {};
                }
                break;
            case InteractionKind::Tap:
                if (primary->pressed && requiredDown) {
                    state.tapCandidate = true;
                    state.pressedAtUs = primary->downAtUs != 0 ? primary->downAtUs : now;
                }
                if (state.tapCandidate && primary->released) {
                    if (now <= state.pressedAtUs + binding.interaction.tapMaxUs) {
                        Emit(resolved, binding, ActionPhase::Pulse, now);
                    }
                    state = {};
                }
                break;
            case InteractionKind::Repeat:
                if (primary->pressed && requiredDown) {
                    state.active = true;
                    state.pressedAtUs = primary->downAtUs != 0 ? primary->downAtUs : now;
                    state.lastRepeatAtUs = 0;
                    Emit(resolved, binding, ActionPhase::Press, now);
                }
                if (state.active && activeByPrimary) {
                    const auto firstRepeatAt = state.pressedAtUs + binding.interaction.repeatDelayUs;
                    const auto nextRepeatAt =
                        state.lastRepeatAtUs == 0 ? firstRepeatAt : state.lastRepeatAtUs + binding.interaction.repeatIntervalUs;
                    if (now >= nextRepeatAt) {
                        state.lastRepeatAtUs = now;
                        Emit(resolved, binding, ActionPhase::Repeat, now);
                    }
                }
                if (state.active && (!primary->down || primary->released || !requiredDown)) {
                    state = {};
                    Emit(resolved, binding, ActionPhase::Release, now);
                }
                break;
            case InteractionKind::Toggle:
                if (primary->pressed && requiredDown) {
                    state.toggleLatched = !state.toggleLatched;
                    Emit(resolved, binding, state.toggleLatched ? ActionPhase::Press : ActionPhase::Release, now);
                }
                break;
            case InteractionKind::Chord: {
                std::uint64_t requiredPressedAt = 0;
                const auto requiredPressed = AnyRequiredPressed(binding, frame, requiredPressedAt);
                const auto shouldFire =
                    requiredDown && primary->down &&
                    (primary->pressed || requiredPressed) &&
                    ChordWindowSatisfied(binding, frame, *primary);
                if (shouldFire && !state.chordLatched) {
                    state.chordLatched = true;
                    Emit(resolved, binding, ActionPhase::Pulse, now);
                    (void)requiredPressedAt;
                }
                if (state.chordLatched && (!primary->down || !requiredDown)) {
                    state.chordLatched = false;
                }
                break;
            }
            default:
                break;
            }
        }

        return resolved;
    }

    std::optional<ControlSample> InteractionEngine::FindSample(
        const InteractionInputFrame& frame,
        const ControlPath& path)
    {
        const auto it = std::find_if(
            frame.samples.begin(),
            frame.samples.end(),
            [&](const ControlSample& sample) {
                return sample.path == path;
            });
        if (it == frame.samples.end()) {
            return std::nullopt;
        }
        return *it;
    }

    float InteractionEngine::ApplyModifiers(float value, const std::vector<BindingModifier>& modifiers)
    {
        auto result = value;
        for (const auto& modifier : modifiers) {
            switch (modifier.kind) {
            case BindingModifierKind::Deadzone:
                if (std::fabs(result) < modifier.primary) {
                    result = 0.0f;
                }
                break;
            case BindingModifierKind::Scale:
                result *= modifier.primary;
                break;
            case BindingModifierKind::Invert:
                result = -result;
                break;
            case BindingModifierKind::Clamp:
                result = std::clamp(result, modifier.primary, modifier.secondary);
                break;
            case BindingModifierKind::AxisThreshold:
                result = std::fabs(result) >= modifier.primary ? result : 0.0f;
                break;
            default:
                break;
            }
        }
        return result;
    }
}
