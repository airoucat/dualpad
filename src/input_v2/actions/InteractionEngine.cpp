#include "pch.h"

#include "input_v2/actions/InteractionEngine.h"

#include "input/PadEvent.h"

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
            std::uint64_t timestampUs,
            std::uint64_t firstEdgeUs = 0,
            std::uint64_t lastEdgeUs = 0,
            std::uint64_t evaluationUs = 0)
        {
            resolved.changes.push_back(ActionPhaseChange{
                .actionId = binding.actionId,
                .bindingId = binding.bindingId,
                .phase = phase,
                .timestampUs = timestampUs,
                .firstEdgeUs = firstEdgeUs,
                .lastEdgeUs = lastEdgeUs,
                .evaluationUs = evaluationUs
            });
        }

        void EmitValue(
            ResolvedActionFrame& resolved,
            const ActionId& actionId,
            BindingId bindingId,
            std::uint64_t timestampUs)
        {
            resolved.changes.push_back(ActionPhaseChange{
                .actionId = actionId,
                .bindingId = bindingId,
                .phase = ActionPhase::Value,
                .timestampUs = timestampUs
            });
        }

        bool IsSamePath(const ControlPath& lhs, const ControlPath& rhs)
        {
            if (lhs.kind == ControlPathKind::AnalogAxis1D &&
                rhs.kind == ControlPathKind::AnalogAxis1D &&
                lhs.code == rhs.code) {
                return lhs.component == rhs.component ||
                    lhs.component == AxisComponent::None ||
                    rhs.component == AxisComponent::None;
            }
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

        float NormalizeAxisValue(float value)
        {
            if (!std::isfinite(value)) {
                return 0.0f;
            }
            const auto clamped = std::clamp(value, -1.0f, 1.0f);
            return std::fabs(clamped) <= 0.0001f ? 0.0f : clamped;
        }

        AxisComponent InferAxisComponent(const ControlPath& path)
        {
            if (path.component != AxisComponent::None) {
                return path.component;
            }

            using dualpad::input::PadAxisId;
            switch (static_cast<PadAxisId>(path.code)) {
            case PadAxisId::LeftStickX:
            case PadAxisId::RightStickX:
                return AxisComponent::X;
            case PadAxisId::LeftStickY:
            case PadAxisId::RightStickY:
                return AxisComponent::Y;
            default:
                return AxisComponent::None;
            }
        }

        const ActionDefinition* FindActionDefinition(
            const CompiledActionGraph& graph,
            std::string_view actionId)
        {
            const auto found = std::find_if(
                graph.actions.begin(),
                graph.actions.end(),
                [&](const ActionDefinition& action) {
                    return action.id == actionId;
                });
            return found == graph.actions.end() ? nullptr : &*found;
        }

        ActionValueKind ValueKindForBinding(
            const CompiledActionGraph& graph,
            const CompiledGraphBinding& binding)
        {
            if (const auto* action = FindActionDefinition(graph, binding.actionId)) {
                return action->valueKind;
            }
            return ActionValueKind::Axis1D;
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

        std::uint64_t EdgeTimestamp(const ControlSample& sample)
        {
            return sample.downAtUs != 0 ? sample.downAtUs : sample.timestampUs;
        }

        struct ChordTiming
        {
            bool satisfied{ false };
            bool hasNewEdge{ false };
            std::uint64_t firstEdgeUs{ 0 };
            std::uint64_t lastEdgeUs{ 0 };
        };

        ChordTiming EvaluateChordTiming(
            const CompiledGraphBinding& binding,
            const KernelFrame& frame,
            const ControlSample& primary)
        {
            ChordTiming timing{};
            timing.hasNewEdge = primary.pressed;
            timing.firstEdgeUs = EdgeTimestamp(primary);
            timing.lastEdgeUs = timing.firstEdgeUs;

            for (const auto requiredIndex : binding.interaction.requiredPathIndices) {
                if (requiredIndex >= binding.paths.size()) {
                    return timing;
                }
                const auto required = InteractionEngine::FindSample(frame, binding.paths[requiredIndex]);
                if (!required.has_value() || !required->down) {
                    return timing;
                }
                timing.hasNewEdge = timing.hasNewEdge || required->pressed;
                const auto requiredAt = EdgeTimestamp(*required);
                timing.firstEdgeUs = (std::min)(timing.firstEdgeUs, requiredAt);
                timing.lastEdgeUs = (std::max)(timing.lastEdgeUs, requiredAt);
            }

            timing.satisfied = timing.lastEdgeUs >= timing.firstEdgeUs &&
                (timing.lastEdgeUs - timing.firstEdgeUs) <= binding.interaction.chordWindowUs;
            return timing;
        }

        struct Axis2DBucket
        {
            ActionId actionId;
            BindingId bindingId{ 0 };
            float x{ 0.0f };
            float y{ 0.0f };
            bool hasX{ false };
            bool hasY{ false };
            bool changed{ false };
            std::uint64_t timestampUs{ 0 };
        };

        void AccumulateAxis2D(
            std::unordered_map<ActionId, Axis2DBucket>& buckets,
            const CompiledGraphBinding& binding,
            const ControlSample& sample,
            float value,
            bool changed,
            std::uint64_t timestampUs)
        {
            auto& bucket = buckets[binding.actionId];
            if (bucket.actionId.empty()) {
                bucket.actionId = binding.actionId;
                bucket.bindingId = binding.bindingId;
            } else {
                bucket.bindingId = (std::min)(bucket.bindingId, binding.bindingId);
            }
            bucket.changed = bucket.changed || changed || sample.pressed || sample.released;
            bucket.timestampUs = (std::max)(bucket.timestampUs, timestampUs);

            switch (InferAxisComponent(sample.path)) {
            case AxisComponent::X:
                bucket.x = value;
                bucket.hasX = true;
                break;
            case AxisComponent::Y:
                bucket.y = value;
                bucket.hasY = true;
                break;
            case AxisComponent::None:
            default:
                if (!bucket.hasX) {
                    bucket.x = value;
                    bucket.hasX = true;
                }
                break;
            }
        }

        void FlushAxis2DValues(
            ResolvedActionFrame& resolved,
            const std::unordered_map<ActionId, Axis2DBucket>& buckets,
            std::uint64_t frameTimestampUs)
        {
            std::vector<const Axis2DBucket*> ordered;
            ordered.reserve(buckets.size());
            for (const auto& [actionId, bucket] : buckets) {
                (void)actionId;
                ordered.push_back(&bucket);
            }
            (std::sort)(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
                return lhs->bindingId < rhs->bindingId;
            });

            for (const auto* bucket : ordered) {
                if (!bucket->changed) {
                    continue;
                }
                const auto timestampUs = frameTimestampUs != 0 ? frameTimestampUs : bucket->timestampUs;
                const auto magnitude = std::clamp(
                    std::sqrt((bucket->x * bucket->x) + (bucket->y * bucket->y)),
                    0.0f,
                    1.0f);
                resolved.values.push_back(ActionValueSnapshot{
                    .actionId = bucket->actionId,
                    .kind = ActionValueKind::Axis2D,
                    .scalar = magnitude,
                    .x = NormalizeAxisValue(bucket->x),
                    .y = NormalizeAxisValue(bucket->y),
                    .timestampUs = timestampUs
                });
                EmitValue(resolved, bucket->actionId, bucket->bindingId, timestampUs);
            }
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
        std::unordered_map<ActionId, Axis2DBucket> axis2DBuckets;
        for (const auto* bindingPtr : selectedBindings) {
            const auto& binding = *bindingPtr;
            if (binding.interaction.primaryPathIndex >= binding.paths.size()) {
                continue;
            }

            if (frame.state.healthDegraded && binding.interaction.kind == InteractionKind::Chord) {
                stateStore.ForBinding(binding.bindingId) = {};
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
                auto value = NormalizeAxisValue(ApplyModifiers(primary->scalar, binding.modifiers));
                const auto changed = std::fabs(value - state.currentScalar) > 0.0001f || primary->pressed || primary->released;
                if (ValueKindForBinding(graph, binding) == ActionValueKind::Axis2D) {
                    state.currentScalar = value;
                    AccumulateAxis2D(axis2DBuckets, binding, *primary, value, changed, now);
                    break;
                }
                if (changed) {
                    state.currentScalar = value;
                    resolved.values.push_back(ActionValueSnapshot{
                        .actionId = binding.actionId,
                        .kind = ActionValueKind::Axis1D,
                        .scalar = value,
                        .x = value,
                        .y = 0.0f,
                        .timestampUs = now
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
                const auto chordTiming = EvaluateChordTiming(binding, frame, *primary);
                const auto shouldFire =
                    requiredDown && primary->down &&
                    chordTiming.hasNewEdge &&
                    chordTiming.satisfied;
                if (shouldFire && !state.chordLatched) {
                    state.chordLatched = true;
                    Emit(
                        resolved,
                        binding,
                        ActionPhase::Pulse,
                        now,
                        chordTiming.firstEdgeUs,
                        chordTiming.lastEdgeUs,
                        now);
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

        FlushAxis2DValues(resolved, axis2DBuckets, frame.facts.monotonicUs);
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
                return IsSamePath(sample.path, path);
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
