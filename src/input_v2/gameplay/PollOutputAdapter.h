#pragma once

#include "input_v2/gameplay/GameplayProjectionFrame.h"

#include <cstdint>
#include <vector>

namespace dualpad::input_v2::gameplay
{
    enum class PollOutputApplyStep : std::uint8_t
    {
        ClearNativeOutput = 0,
        ClearHelperOutput,
        ClearSustainedDigitalAggregator,
        ClearProjectionStickyOwners,
        ApplyGatePlan,
        ApplySustainedDigital,
        ApplyTransientDigital,
        ApplyHelperCommand,
        PublishAnalogState,
        CommitCleanRecoveryBaseline
    };

    struct PollOutputApplyResult
    {
        bool outputApplySucceeded{ false };
        std::vector<PollOutputApplyStep> steps;
    };

    class IPollOutputExecutor
    {
    public:
        virtual ~IPollOutputExecutor() = default;

        virtual bool ClearNativeOutput() = 0;
        virtual bool ClearHelperOutput() = 0;
        virtual bool ClearSustainedDigitalAggregator() = 0;
        virtual bool ClearProjectionStickyOwners() = 0;
        virtual bool ApplyGatePlan(const GatePlan& gatePlan) = 0;
        virtual bool ApplySustainedDigital(const NativeSustainedCommand& command) = 0;
        virtual bool ApplyTransientDigital(const NativeTransientCommand& command) = 0;
        virtual bool ApplyHelperCommand(const HelperOutputCommand& command) = 0;
        virtual bool PublishAnalogState(const ProjectedAnalogState& analog) = 0;
        virtual bool CommitCleanRecoveryBaseline() = 0;
    };

    class PollOutputAdapter
    {
    public:
        PollOutputApplyResult Apply(
            const GameplayProjectionFrame& frame,
            IPollOutputExecutor& executor) const;
    };
}
