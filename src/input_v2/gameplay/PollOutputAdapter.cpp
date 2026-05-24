#include "pch.h"

#include "input_v2/gameplay/PollOutputAdapter.h"

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        bool RunStep(
            PollOutputApplyResult& result,
            PollOutputApplyStep step,
            bool succeeded)
        {
            result.steps.push_back(step);
            if (!succeeded) {
                result.outputApplySucceeded = false;
                return false;
            }
            return true;
        }
    }

    PollOutputApplyResult PollOutputAdapter::Apply(
        const GameplayProjectionFrame& frame,
        IPollOutputExecutor& executor) const
    {
        PollOutputApplyResult result{};

        if (frame.recoveryPlan.resetNativeCommitBackend) {
            if (!RunStep(result, PollOutputApplyStep::ClearNativeOutput, executor.ClearNativeOutput())) {
                return result;
            }
        }

        if (frame.recoveryPlan.resetKeyboardHelperBackend || frame.helperPlan.enqueueBridgeResetBeforeApply) {
            if (!RunStep(result, PollOutputApplyStep::ClearHelperOutput, executor.ClearHelperOutput())) {
                return result;
            }
        }

        if (frame.recoveryPlan.resetSustainedDigitalAggregator) {
            if (!RunStep(
                    result,
                    PollOutputApplyStep::ClearSustainedDigitalAggregator,
                    executor.ClearSustainedDigitalAggregator())) {
                return result;
            }
        }

        if (frame.recoveryPlan.clearProjectionStickyOwners) {
            if (!RunStep(
                    result,
                    PollOutputApplyStep::ClearProjectionStickyOwners,
                    executor.ClearProjectionStickyOwners())) {
                return result;
            }
        }

        if (!RunStep(result, PollOutputApplyStep::ApplyGatePlan, executor.ApplyGatePlan(frame.gatePlan))) {
            return result;
        }

        for (std::size_t index = 0; index < frame.gamepadPlan.sustainedDigital.count; ++index) {
            if (!RunStep(
                    result,
                    PollOutputApplyStep::ApplySustainedDigital,
                    executor.ApplySustainedDigital(frame.gamepadPlan.sustainedDigital.items[index]))) {
                return result;
            }
        }

        for (std::size_t index = 0; index < frame.gamepadPlan.transientDigital.count; ++index) {
            if (!RunStep(
                    result,
                    PollOutputApplyStep::ApplyTransientDigital,
                    executor.ApplyTransientDigital(frame.gamepadPlan.transientDigital.items[index]))) {
                return result;
            }
        }

        for (std::size_t index = 0; index < frame.helperPlan.commands.count; ++index) {
            if (!RunStep(
                    result,
                    PollOutputApplyStep::ApplyHelperCommand,
                    executor.ApplyHelperCommand(frame.helperPlan.commands.items[index]))) {
                return result;
            }
        }

        if (!RunStep(result, PollOutputApplyStep::PublishAnalogState, executor.PublishAnalogState(frame.gamepadPlan.analog))) {
            return result;
        }

        if (frame.recoveryPlan.commitCleanRecoveryBaselineAfterApply) {
            if (!RunStep(
                    result,
                    PollOutputApplyStep::CommitCleanRecoveryBaseline,
                    executor.CommitCleanRecoveryBaseline())) {
                return result;
            }
        }

        result.outputApplySucceeded = true;
        return result;
    }
}
