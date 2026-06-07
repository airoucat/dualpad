#include "pch.h"

#include "input_v2/gameplay/RuntimeDiagnostics.h"

#include <SKSE/Logger.h>

#include <sstream>

namespace logger = SKSE::log;

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        std::string Join(const std::vector<std::string>& values, std::string_view separator)
        {
            std::ostringstream out;
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0) {
                    out << separator;
                }
                out << values[i];
            }
            return out.str();
        }

        RuntimePromptDebugState ResolvePromptState(const RuntimeDebugProjectionInput& input)
        {
            if (input.frame.kind == ingress::AssembledFrameKind::Transition) {
                return RuntimePromptDebugState::Unavailable;
            }
            if (HasRuntimeHealthReason(input.runtimeHealthReasons, RuntimeHealthReason::PromptScopeFrozen)) {
                return RuntimePromptDebugState::Frozen;
            }
            if (!input.outputApplySucceeded) {
                return RuntimePromptDebugState::Unavailable;
            }
            return RuntimePromptDebugState::Ready;
        }

        std::string ResolvePromptDebugReason(
            const RuntimeDebugProjectionInput& input,
            RuntimePromptDebugState state,
            std::string_view reasonSummary)
        {
            switch (state) {
            case RuntimePromptDebugState::Ready:
                return "published";
            case RuntimePromptDebugState::Frozen:
                return std::string("runtime_health_degraded:") + std::string(reasonSummary);
            case RuntimePromptDebugState::Unavailable:
            default:
                if (input.frame.kind == ingress::AssembledFrameKind::Transition) {
                    return "transition_frame";
                }
                if (!input.outputApplySucceeded) {
                    return "output_apply_failed";
                }
                return "prompt_scope_unavailable";
            }
        }

        std::string BuildLogKey(const RuntimeDebugSnapshot& snapshot)
        {
            std::ostringstream key;
            key << "degraded=" << (snapshot.runtimeHealthDegraded ? "true" : "false")
                << "|mask=" << snapshot.runtimeHealthReasons
                << "|debug=" << snapshot.runtimeHealthDebugReason
                << "|hook=" << snapshot.hookInstallStatusName
                << "|hook_reason=" << snapshot.hookInstallDebugReason
                << "|prompt=" << snapshot.promptStateName
                << "|prompt_reason=" << snapshot.promptDebugReason
                << "|overflow=" << snapshot.overflowCompactionSummary
                << "|transition=" << snapshot.transitionReason;
            return key.str();
        }
    }

    const char* ToString(RuntimeHealthReason reason)
    {
        switch (reason) {
        case RuntimeHealthReason::None:
            return "None";
        case RuntimeHealthReason::GraphUnavailable:
            return "GraphUnavailable";
        case RuntimeHealthReason::ManifestEpochSkew:
            return "ManifestEpochSkew";
        case RuntimeHealthReason::ContextRevisionSkew:
            return "ContextRevisionSkew";
        case RuntimeHealthReason::QueueOverflow:
            return "QueueOverflow";
        case RuntimeHealthReason::SequenceGap:
            return "SequenceGap";
        case RuntimeHealthReason::BoundaryMismatch:
            return "BoundaryMismatch";
        case RuntimeHealthReason::PromptScopeFrozen:
            return "PromptScopeFrozen";
        case RuntimeHealthReason::HookInstallFailed:
            return "HookInstallFailed";
        default:
            return "Unknown";
        }
    }

    const char* ToString(RuntimePromptDebugState state)
    {
        switch (state) {
        case RuntimePromptDebugState::Ready:
            return "ready";
        case RuntimePromptDebugState::Frozen:
            return "frozen";
        case RuntimePromptDebugState::Unavailable:
        default:
            return "unavailable";
        }
    }

    const char* ToString(ingress::AssembledFrameKind kind)
    {
        switch (kind) {
        case ingress::AssembledFrameKind::Stable:
            return "stable";
        case ingress::AssembledFrameKind::Transition:
        default:
            return "transition";
        }
    }

    const char* ToString(ingress::TransitionReason reason)
    {
        switch (reason) {
        case ingress::TransitionReason::BoundaryKeyChanged:
            return "boundary_key_changed";
        case ingress::TransitionReason::ManifestEpochChanged:
            return "manifest_epoch_changed";
        case ingress::TransitionReason::SequenceGap:
            return "sequence_gap";
        case ingress::TransitionReason::QueueOverflow:
            return "queue_overflow";
        case ingress::TransitionReason::ExplicitReset:
        default:
            return "explicit_reset";
        }
    }

    std::vector<std::string> RuntimeHealthReasonNames(RuntimeHealthReasonMask mask)
    {
        std::vector<std::string> names;
        for (const auto reason : {
                 RuntimeHealthReason::GraphUnavailable,
                 RuntimeHealthReason::ManifestEpochSkew,
                 RuntimeHealthReason::ContextRevisionSkew,
                 RuntimeHealthReason::QueueOverflow,
                 RuntimeHealthReason::SequenceGap,
                 RuntimeHealthReason::BoundaryMismatch,
                 RuntimeHealthReason::PromptScopeFrozen,
                 RuntimeHealthReason::HookInstallFailed }) {
            if (HasRuntimeHealthReason(mask, reason)) {
                names.emplace_back(ToString(reason));
            }
        }
        if (names.empty()) {
            names.emplace_back(ToString(RuntimeHealthReason::None));
        }
        return names;
    }

    std::string RuntimeHealthReasonSummary(RuntimeHealthReasonMask mask)
    {
        return Join(RuntimeHealthReasonNames(mask), "|");
    }

    RuntimeDebugSnapshot ProjectRuntimeDebugSnapshot(const RuntimeDebugProjectionInput& input)
    {
        const auto reasonNames = RuntimeHealthReasonNames(input.runtimeHealthReasons);
        const auto reasonSummary = Join(reasonNames, "|");
        const auto promptState = ResolvePromptState(input);
        RuntimeDebugSnapshot snapshot{
            .firstSeq = input.frame.firstSeq,
            .lastSeq = input.frame.lastSeq,
            .frameKind = ToString(input.frame.kind),
            .transitionReason = input.frame.kind == ingress::AssembledFrameKind::Transition ?
                ToString(input.frame.transition.reason) :
                "",
            .runtimeHealthDegraded = input.runtimeHealthReasons != RuntimeHealthMask(RuntimeHealthReason::None),
            .runtimeHealthReasons = input.runtimeHealthReasons,
            .runtimeHealthReasonNames = reasonNames,
            .runtimeHealthReasonSummary = reasonSummary,
            .runtimeHealthDebugReason = std::string(input.runtimeHealthDebugReason),
            .hookInstallStatus = input.hookInstall.status,
            .hookInstallStatusName = presentation::ToString(input.hookInstall.status),
            .hookInstallDebugReason = input.hookInstall.debugReason,
            .hookInstallDebugSummary = presentation::ToDebugString(input.hookInstall),
            .promptState = promptState,
            .promptStateName = ToString(promptState),
            .promptDebugReason = ResolvePromptDebugReason(input, promptState, reasonSummary),
            .overflowTransition = input.frame.kind == ingress::AssembledFrameKind::Transition &&
                input.frame.transition.reason == ingress::TransitionReason::QueueOverflow
        };

        if (input.frame.facts.overflowCompaction) {
            snapshot.overflowTransition = snapshot.overflowTransition ||
                input.frame.facts.overflowCompaction->transitionObserved;
            snapshot.overflowTypedCompaction = input.frame.facts.overflowCompaction->typedCompactionApplied;
            snapshot.overflowCompactionSummary = input.frame.facts.overflowCompaction->debugSummary;
        } else if (snapshot.overflowTransition) {
            snapshot.overflowCompactionSummary = "overflow_transition=true typed_compaction=false";
        }

        return snapshot;
    }

    bool ShouldEmitRuntimeDebugLog(RuntimeDiagnosticsLogState& state, const RuntimeDebugSnapshot& snapshot)
    {
        const auto key = BuildLogKey(snapshot);
        if (state.hasLastKey && state.lastKey == key) {
            return false;
        }

        const bool shouldEmit = snapshot.runtimeHealthDegraded ||
            (state.hasLastKey && state.lastRuntimeHealthDegraded && !snapshot.runtimeHealthDegraded);
        state.hasLastKey = true;
        state.lastRuntimeHealthDegraded = snapshot.runtimeHealthDegraded;
        state.lastKey = key;
        return shouldEmit;
    }

    void LogRuntimeDebugSnapshotTransition(RuntimeDiagnosticsLogState& state, const RuntimeDebugSnapshot& snapshot)
    {
        if (!ShouldEmitRuntimeDebugLog(state, snapshot)) {
            return;
        }

        if (snapshot.runtimeHealthDegraded) {
            logger::warn(
                "[DualPad][RuntimeDebug] degraded reasons={} debug='{}' prompt_state={} prompt_reason={} hook_status={} hook_reason='{}' overflow='{}'",
                snapshot.runtimeHealthReasonSummary,
                snapshot.runtimeHealthDebugReason,
                snapshot.promptStateName,
                snapshot.promptDebugReason,
                snapshot.hookInstallStatusName,
                snapshot.hookInstallDebugReason,
                snapshot.overflowCompactionSummary);
            return;
        }

        logger::info(
            "[DualPad][RuntimeDebug] recovered prompt_state={} hook_status={}",
            snapshot.promptStateName,
            snapshot.hookInstallStatusName);
    }
}
