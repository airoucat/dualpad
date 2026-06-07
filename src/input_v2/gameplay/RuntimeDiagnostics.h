#pragma once

#include "input_v2/gameplay/RuntimeFrameEnvelope.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::input_v2::gameplay
{
    enum class RuntimePromptDebugState : std::uint8_t
    {
        Ready = 0,
        Frozen,
        Unavailable
    };

    struct RuntimeDebugSnapshot
    {
        std::uint64_t firstSeq{ 0 };
        std::uint64_t lastSeq{ 0 };
        std::string frameKind;
        std::string transitionReason;
        bool runtimeHealthDegraded{ false };
        RuntimeHealthReasonMask runtimeHealthReasons{ RuntimeHealthMask(RuntimeHealthReason::None) };
        std::vector<std::string> runtimeHealthReasonNames;
        std::string runtimeHealthReasonSummary;
        std::string runtimeHealthDebugReason;

        presentation::HookInstallStatus hookInstallStatus{ presentation::HookInstallStatus::NotAttempted };
        std::string hookInstallStatusName;
        std::string hookInstallDebugReason;
        std::string hookInstallDebugSummary;

        RuntimePromptDebugState promptState{ RuntimePromptDebugState::Unavailable };
        std::string promptStateName;
        std::string promptDebugReason;

        bool overflowTransition{ false };
        bool overflowTypedCompaction{ false };
        std::string overflowCompactionSummary;
    };

    struct RuntimeDebugProjectionInput
    {
        const ingress::AssembledFactFrame& frame;
        RuntimeHealthReasonMask runtimeHealthReasons{ RuntimeHealthMask(RuntimeHealthReason::None) };
        std::string_view runtimeHealthDebugReason;
        bool outputApplySucceeded{ false };
        presentation::HookInstallResult hookInstall;
    };

    struct RuntimeDiagnosticsLogState
    {
        bool hasLastKey{ false };
        bool lastRuntimeHealthDegraded{ false };
        std::string lastKey;
    };

    const char* ToString(RuntimeHealthReason reason);
    const char* ToString(RuntimePromptDebugState state);
    const char* ToString(ingress::AssembledFrameKind kind);
    const char* ToString(ingress::TransitionReason reason);

    std::vector<std::string> RuntimeHealthReasonNames(RuntimeHealthReasonMask mask);
    std::string RuntimeHealthReasonSummary(RuntimeHealthReasonMask mask);
    RuntimeDebugSnapshot ProjectRuntimeDebugSnapshot(const RuntimeDebugProjectionInput& input);
    bool ShouldEmitRuntimeDebugLog(RuntimeDiagnosticsLogState& state, const RuntimeDebugSnapshot& snapshot);
    void LogRuntimeDebugSnapshotTransition(RuntimeDiagnosticsLogState& state, const RuntimeDebugSnapshot& snapshot);
}
