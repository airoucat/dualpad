#include "pch.h"

#include "input_v2/actions/InteractionEngine.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace dualpad::input_v2::actions
{
    namespace
    {
        enum class BindingMatchStrength : std::uint8_t
        {
            None = 0,
            Subset,
            Exact
        };

        struct BindingCandidate
        {
            const CompiledGraphBinding* binding{ nullptr };
            BindingMatchStrength strength{ BindingMatchStrength::None };
            std::size_t specificity{ 0 };
        };

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

        bool IsSamePath(const ControlPath& lhs, const ControlPath& rhs)
        {
            return lhs == rhs;
        }

        bool ContainsPath(const std::vector<ControlPath>& paths, const ControlPath& path)
        {
            return std::find_if(
                paths.begin(),
                paths.end(),
                [&](const ControlPath& candidate) {
                    return IsSamePath(candidate, path);
                }) != paths.end();
        }

        bool IsPathActiveForMatch(const ControlSample& sample)
        {
            if (sample.path.kind == ControlPathKind::AnalogAxis1D) {
                return std::fabs(sample.scalar) > 0.0001f;
            }
            return sample.down;
        }

        bool IsBindingPathActive(
            const KernelFrame& frame,
            const ControlPath& path,
            InteractionKind kind)
        {
            const auto sample = InteractionEngine::FindSample(frame, path);
            if (!sample.has_value()) {
                return false;
            }
            if (kind == InteractionKind::Value) {
                return true;
            }
            return sample->down || sample->pressed;
        }

        std::vector<ControlPath> ActivePathsForPrimaryKind(
            const KernelFrame& frame,
            const ControlPath& primaryPath)
        {
            std::vector<ControlPath> active;
            for (const auto& sample : frame.state.controlSamples) {
                if (sample.path.kind != primaryPath.kind) {
                    continue;
                }
                if (!IsPathActiveForMatch(sample)) {
                    continue;
                }
                if (!ContainsPath(active, sample.path)) {
                    active.push_back(sample.path);
                }
            }
            return active;
        }

        BindingMatchStrength EvaluateBindingMatch(
            const CompiledGraphBinding& binding,
            const KernelFrame& frame)
        {
            if (binding.interaction.primaryPathIndex >= binding.paths.size()) {
                return BindingMatchStrength::None;
            }

            for (const auto& path : binding.paths) {
                if (!IsBindingPathActive(frame, path, binding.interaction.kind)) {
                    return BindingMatchStrength::None;
                }
            }

            if (binding.interaction.kind == InteractionKind::Value) {
                return BindingMatchStrength::Exact;
            }

            const auto& primaryPath = binding.paths[binding.interaction.primaryPathIndex];
            const auto activePaths = ActivePathsForPrimaryKind(frame, primaryPath);
            bool hasExtraActivePath = false;
            for (const auto& activePath : activePaths) {
                if (!ContainsPath(binding.paths, activePath)) {
                    hasExtraActivePath = true;
                    break;
                }
            }

            if (!hasExtraActivePath) {
                return BindingMatchStrength::Exact;
            }
            if (binding.matchPolicy == BindingMatchPolicy::PreferExactThenSubset) {
                return BindingMatchStrength::Subset;
            }
            return BindingMatchStrength::None;
        }

        bool IsLiveState(const InteractionBindingState* state)
        {
            return state != nullptr &&
                (state->active || state->holdFired || state->tapCandidate || state->chordLatched);
        }

        std::vector<const CompiledGraphBinding*> SelectBindingsForFrame(
            const std::vector<const CompiledGraphBinding*>& visibleBindings,
            const KernelFrame& frame,
            const InteractionStateStore& stateStore)
        {
            std::vector<const CompiledGraphBinding*> selected;
            std::unordered_set<BindingId> selectedIds;
            std::unordered_map<ControlPath, std::vector<BindingCandidate>, ControlPathHash> candidatesByPrimaryPath;

            for (const auto* binding : visibleBindings) {
                if (binding == nullptr || binding->interaction.primaryPathIndex >= binding->paths.size()) {
                    continue;
                }

                if (IsLiveState(stateStore.Find(binding->bindingId))) {
                    selected.push_back(binding);
                    selectedIds.insert(binding->bindingId);
                    continue;
                }

                const auto strength = EvaluateBindingMatch(*binding, frame);
                if (strength == BindingMatchStrength::None) {
                    continue;
                }

                const auto& primaryPath = binding->paths[binding->interaction.primaryPathIndex];
                candidatesByPrimaryPath[primaryPath].push_back(BindingCandidate{
                    .binding = binding,
                    .strength = strength,
                    .specificity = binding->paths.size()
                });
            }

            for (auto& [primaryPath, candidates] : candidatesByPrimaryPath) {
                (void)primaryPath;
                (std::sort)(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
                    if (lhs.strength != rhs.strength) {
                        return static_cast<std::uint8_t>(lhs.strength) > static_cast<std::uint8_t>(rhs.strength);
                    }
                    if (lhs.specificity != rhs.specificity) {
                        return lhs.specificity > rhs.specificity;
                    }
                    return lhs.binding->bindingId < rhs.binding->bindingId;
                });

                if (!candidates.empty() && !selectedIds.contains(candidates.front().binding->bindingId)) {
                    selected.push_back(candidates.front().binding);
                    selectedIds.insert(candidates.front().binding->bindingId);
                }
            }

            (std::sort)(selected.begin(), selected.end(), [](const auto* lhs, const auto* rhs) {
                return lhs->bindingId < rhs->bindingId;
            });
            return selected;
        }

        bool RequiredPathsDown(
            const CompiledGraphBinding& binding,
            const KernelFrame& frame)
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
            const KernelFrame& frame,
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
            const KernelFrame& frame,
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
        const KernelFrame& frame,
        InteractionStateStore& stateStore) const
    {
        ResolvedActionFrame resolved{};
        resolved.manifestEpoch = frame.facts.manifestEpoch;
        resolved.contextRevision = frame.facts.contextRevision;

        if (frame.facts.manifestEpoch != graph.manifestEpoch) {
            return resolved;
        }

        const auto visibleBindings = graph.BindingsForActionSet(actionSetStack.baseSetId, actionSetStack.layerIds);
        const auto selectedBindings = SelectBindingsForFrame(visibleBindings, frame, stateStore);
        for (const auto* bindingPtr : selectedBindings) {
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
            const auto now = frame.facts.monotonicUs != 0 ? frame.facts.monotonicUs : primary->timestampUs;

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
        const KernelFrame& frame,
        const ControlPath& path)
    {
        const auto it = std::find_if(
            frame.state.controlSamples.begin(),
            frame.state.controlSamples.end(),
            [&](const ControlSample& sample) {
                return sample.path == path;
            });
        if (it == frame.state.controlSamples.end()) {
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
