#include "pch.h"

#include "input_v2/gameplay/RuntimeInputPublication.h"

#include <utility>

namespace dualpad::input_v2::gameplay
{
    RuntimeInputPublication& RuntimeInputPublication::GetSingleton()
    {
        static RuntimeInputPublication publication;
        return publication;
    }

    PreparedRuntimeInputCommit RuntimeInputPublication::Prepare(
        const CurrentCycleSensitiveState& proposed,
        const CurrentCycleGatePlan& plan)
    {
        std::scoped_lock lock(_mutex);
        PreparedRuntimeInputCommit prepared{
            .token = ++_nextToken,
            .previous = _committed,
            .proposed = proposed,
            .plan = plan
        };
        _prepared.emplace(prepared.token, PreparedRecord{ prepared });
        return prepared;
    }

    RuntimeInputCommitResult RuntimeInputPublication::CommitAfterCurrentCycleAudit(
        std::uint64_t token,
        const CurrentCycleAdapterAudit& audit)
    {
        std::scoped_lock lock(_mutex);
        if (_consumed.contains(token)) {
            return { .alreadyConsumed = true };
        }
        const auto found = _prepared.find(token);
        if (found == _prepared.end()) {
            return { .invalidToken = true };
        }

        const auto prepared = found->second.prepared;
        _prepared.erase(found);
        _consumed.insert(token);
        if (!IsCurrentCycleAuditCommitSafe(prepared.plan, audit)) {
            return {
                .failClosedChannels = static_cast<CurrentCycleChannelMaskType>(
                    audit.affectedChannels | prepared.plan.affectedChannels)
            };
        }

        _committed = prepared.proposed;
        return { .committed = true };
    }

    CurrentCycleSensitiveState RuntimeInputPublication::GetCommitted() const
    {
        std::scoped_lock lock(_mutex);
        return _committed;
    }

    EngineModeDecisionSnapshot RuntimeInputPublication::PublishOriginalEngineModeShadow(
        std::uint64_t ownerTickToken,
        std::uint64_t inputStateEpoch,
        std::uint32_t contextRevision)
    {
        std::scoped_lock lock(_mutex);
        _engineModeShadow = ProjectOriginalEngineModes(
            ownerTickToken,
            inputStateEpoch,
            contextRevision,
            _committed.revision);
        return _engineModeShadow;
    }

    EngineModeDecisionSnapshot RuntimeInputPublication::GetEngineModeShadow() const
    {
        std::scoped_lock lock(_mutex);
        return _engineModeShadow;
    }

    PreparedCurrentCycleCallback RuntimeInputPublication::PrepareCallbackAudit(
        std::uint64_t ownerTickToken,
        const CurrentCycleGatePlan& plan,
        CurrentCycleCallbackEvidence evidence)
    {
        if (ownerTickToken == 0) {
            return {};
        }
        std::scoped_lock lock(_mutex);
        _activeCallbackOwnerTickToken = ownerTickToken;
        PreparedCurrentCycleCallback prepared{
            .token = ++_nextCallbackToken,
            .ownerTickToken = ownerTickToken,
            .plan = plan,
            .evidence = std::move(evidence)
        };
        _preparedCallbacks.emplace(prepared.token, prepared);
        return prepared;
    }

    bool RuntimeInputPublication::CommitCallbackAudit(
        std::uint64_t token,
        const CurrentCycleAdapterAudit& audit)
    {
        std::scoped_lock lock(_mutex);
        if (_consumedCallbackTokens.contains(token)) {
            return false;
        }
        const auto found = _preparedCallbacks.find(token);
        if (found == _preparedCallbacks.end()) {
            return false;
        }
        const auto prepared = found->second;
        _preparedCallbacks.erase(found);
        _consumedCallbackTokens.insert(token);
        _callbackAudits[prepared.ownerTickToken] = PublishedCurrentCycleAudit{
            .ownerTickToken = prepared.ownerTickToken,
            .plan = prepared.plan,
            .audit = audit,
            .evidence = prepared.evidence
        };
        return true;
    }

    std::optional<PublishedCurrentCycleAudit> RuntimeInputPublication::FindCallbackAudit(
        std::uint64_t ownerTickToken) const
    {
        std::scoped_lock lock(_mutex);
        const auto found = _callbackAudits.find(ownerTickToken);
        return found == _callbackAudits.end() ? std::nullopt : std::optional{ found->second };
    }

    std::optional<PublishedCurrentCycleAudit> RuntimeInputPublication::FindActiveCallbackAudit() const
    {
        std::scoped_lock lock(_mutex);
        const auto found = _callbackAudits.find(_activeCallbackOwnerTickToken);
        return found == _callbackAudits.end() ? std::nullopt : std::optional{ found->second };
    }

    void RuntimeInputPublication::ClearCallbackAudit(std::uint64_t ownerTickToken)
    {
        std::scoped_lock lock(_mutex);
        _callbackAudits.erase(ownerTickToken);
        if (_activeCallbackOwnerTickToken == ownerTickToken) {
            _activeCallbackOwnerTickToken = 0;
        }
    }

    void RuntimeInputPublication::ResetCommittedState(CurrentCycleSensitiveState initial)
    {
        std::scoped_lock lock(_mutex);
        _committed = std::move(initial);
        _engineModeShadow = EngineModeDecisionSnapshot{};
        _prepared.clear();
        _consumed.clear();
        _nextToken = 0;
    }

    void RuntimeInputPublication::Reset(CurrentCycleSensitiveState initial)
    {
        std::scoped_lock lock(_mutex);
        _committed = std::move(initial);
        _engineModeShadow = EngineModeDecisionSnapshot{};
        _prepared.clear();
        _consumed.clear();
        _callbackAudits.clear();
        _preparedCallbacks.clear();
        _consumedCallbackTokens.clear();
        _nextToken = 0;
        _nextCallbackToken = 0;
        _activeCallbackOwnerTickToken = 0;
    }

    void RuntimeInputPublication::ResetForTests(CurrentCycleSensitiveState initial)
    {
        Reset(std::move(initial));
    }
}
