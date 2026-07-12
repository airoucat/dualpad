#pragma once

#include "input_v2/gameplay/ChannelArbitration.h"
#include "input_v2/gameplay/CurrentCycleGatePlan.h"
#include "input_v2/gameplay/TransientActionGate.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace dualpad::input_v2::gameplay
{
    struct CurrentCycleSensitiveState
    {
        ChannelArbitrationStateSet channels{};
        TransientActionGateState transient{};
        std::uint8_t sprintContributorMask{ 0 };
        bool virtualSprintBridgeHeld{ false };
        std::uint64_t revision{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        std::uint64_t orderedCutoffSeq{ 0 };
        std::uint64_t eventBatchToken{ 0 };
    };

    struct PreparedRuntimeInputCommit
    {
        std::uint64_t token{ 0 };
        CurrentCycleSensitiveState previous{};
        CurrentCycleSensitiveState proposed{};
        CurrentCycleGatePlan plan{};
    };

    struct RuntimeInputCommitResult
    {
        bool committed{ false };
        bool alreadyConsumed{ false };
        bool invalidToken{ false };
        CurrentCycleChannelMaskType failClosedChannels{ 0 };
    };

    struct PublishedCurrentCycleAudit
    {
        std::uint64_t ownerTickToken{ 0 };
        CurrentCycleGatePlan plan{};
        CurrentCycleAdapterAudit audit{};
    };

    struct PreparedCurrentCycleCallback
    {
        std::uint64_t token{ 0 };
        std::uint64_t ownerTickToken{ 0 };
        CurrentCycleGatePlan plan{};
    };

    class RuntimeInputPublication
    {
    public:
        static RuntimeInputPublication& GetSingleton();

        PreparedRuntimeInputCommit Prepare(
            const CurrentCycleSensitiveState& proposed,
            const CurrentCycleGatePlan& plan);
        RuntimeInputCommitResult CommitAfterCurrentCycleAudit(
            std::uint64_t token,
            const CurrentCycleAdapterAudit& audit);
        CurrentCycleSensitiveState GetCommitted() const;
        PreparedCurrentCycleCallback PrepareCallbackAudit(
            std::uint64_t ownerTickToken,
            const CurrentCycleGatePlan& plan);
        bool CommitCallbackAudit(
            std::uint64_t token,
            const CurrentCycleAdapterAudit& audit);
        std::optional<PublishedCurrentCycleAudit> FindCallbackAudit(
            std::uint64_t ownerTickToken) const;
        std::optional<PublishedCurrentCycleAudit> FindActiveCallbackAudit() const;
        void ClearCallbackAudit(std::uint64_t ownerTickToken);
        void ResetCommittedState(CurrentCycleSensitiveState initial = {});
        void Reset(CurrentCycleSensitiveState initial = {});
        void ResetForTests(CurrentCycleSensitiveState initial = {});

    private:
        struct PreparedRecord
        {
            PreparedRuntimeInputCommit prepared{};
        };

        mutable std::mutex _mutex;
        CurrentCycleSensitiveState _committed{};
        std::unordered_map<std::uint64_t, PreparedRecord> _prepared;
        std::unordered_set<std::uint64_t> _consumed;
        std::unordered_map<std::uint64_t, PublishedCurrentCycleAudit> _callbackAudits;
        std::unordered_map<std::uint64_t, PreparedCurrentCycleCallback> _preparedCallbacks;
        std::unordered_set<std::uint64_t> _consumedCallbackTokens;
        std::uint64_t _nextToken{ 0 };
        std::uint64_t _nextCallbackToken{ 0 };
        std::uint64_t _activeCallbackOwnerTickToken{ 0 };
    };
}
