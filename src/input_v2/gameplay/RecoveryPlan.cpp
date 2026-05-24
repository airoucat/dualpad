#include "pch.h"

#include "input_v2/gameplay/RecoveryPlan.h"

namespace dualpad::input_v2::gameplay
{
    RecoveryPlan BuildRecoveryPlan(const GameplayRecoveryInput& input)
    {
        RecoveryPlan plan{};
        const bool hardReset = input.hardResetRequested || input.explicitResetRequested;
        const bool softResync = input.softResyncRequested || input.sequenceGapObserved;

        if (!hardReset && !softResync) {
            plan.commitCleanRecoveryBaselineAfterApply = input.cleanFrame;
            return plan;
        }

        plan.mode = hardReset ? RecoveryMode::HardResetOutputs : RecoveryMode::SoftResyncOutputs;
        plan.resetNativeCommitBackend = true;
        plan.resetKeyboardHelperBackend = true;
        plan.resetSustainedDigitalAggregator = true;
        plan.clearProjectionStickyOwners = true;
        plan.clearRecoveryBaseline = hardReset;
        plan.commitCleanRecoveryBaselineAfterApply = input.cleanFrame;
        return plan;
    }

    std::vector<RecoveryExecutionStep> BuildRecoveryExecutionPlan(const RecoveryPlan& plan)
    {
        std::vector<RecoveryExecutionStep> steps;
        if (plan.mode != RecoveryMode::None) {
            if (plan.resetNativeCommitBackend || plan.resetKeyboardHelperBackend) {
                steps.push_back(RecoveryExecutionStep::ClearOutputState);
            }
            if (plan.resetSustainedDigitalAggregator) {
                steps.push_back(RecoveryExecutionStep::ClearSustainedAggregator);
            }
            if (plan.clearProjectionStickyOwners) {
                steps.push_back(RecoveryExecutionStep::ClearProjectionStickyOwners);
            }
        }

        steps.push_back(RecoveryExecutionStep::ApplyOutputPlans);

        if (plan.commitCleanRecoveryBaselineAfterApply) {
            steps.push_back(RecoveryExecutionStep::CommitCleanRecoveryBaseline);
        }

        return steps;
    }
}
