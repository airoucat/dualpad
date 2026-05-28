#pragma once

#include <cstdint>
#include <vector>

namespace dualpad::input_v2::gameplay
{
    enum class RecoveryMode : std::uint8_t
    {
        None = 0,
        SoftResyncOutputs,
        HardResetOutputs
    };

    struct RecoveryPlan
    {
        RecoveryMode mode{ RecoveryMode::None };
        bool resetNativeCommitBackend{ false };
        bool resetKeyboardHelperBackend{ false };
        bool resetSustainedDigitalAggregator{ false };
        bool clearProjectionStickyOwners{ false };
        bool clearRecoveryBaseline{ false };
        bool commitCleanRecoveryBaselineAfterApply{ false };
    };

    struct GameplayRecoveryInput
    {
        bool softResyncRequested{ false };
        bool hardResetRequested{ false };
        bool sequenceGapObserved{ false };
        bool explicitResetRequested{ false };
        bool cleanFrame{ false };
    };

    enum class RecoveryExecutionStep : std::uint8_t
    {
        ClearOutputState = 0,
        ClearSustainedAggregator,
        ClearProjectionStickyOwners,
        ApplyOutputPlans,
        CommitCleanRecoveryBaseline
    };

    RecoveryPlan BuildRecoveryPlan(const GameplayRecoveryInput& input);
    std::vector<RecoveryExecutionStep> BuildRecoveryExecutionPlan(const RecoveryPlan& plan);
}
