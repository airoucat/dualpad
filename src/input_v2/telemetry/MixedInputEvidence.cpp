#include "pch.h"

#include "input_v2/telemetry/MixedInputEvidence.h"

#include "input/RuntimeConfig.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace dualpad::input_v2::telemetry
{
    namespace
    {
        const char* Bool(bool value)
        {
            return value ? "true" : "false";
        }

        bool HasChannel(
            gameplay::CurrentCycleChannelMaskType mask,
            gameplay::CurrentCycleChannel channel)
        {
            return (mask & gameplay::CurrentCycleChannelMask(channel)) != 0;
        }

        const char* GateFailureName(gameplay::CurrentCycleGateFailure failure)
        {
            using gameplay::CurrentCycleGateFailure;
            switch (failure) {
            case CurrentCycleGateFailure::None: return "None";
            case CurrentCycleGateFailure::MissingReceipt: return "MissingReceipt";
            case CurrentCycleGateFailure::AmbiguousReceipt: return "AmbiguousReceipt";
            case CurrentCycleGateFailure::AlreadyConsumedReceipt: return "AlreadyConsumedReceipt";
            case CurrentCycleGateFailure::ThreadMismatch: return "ThreadMismatch";
            case CurrentCycleGateFailure::PollFrameMismatch: return "PollFrameMismatch";
            case CurrentCycleGateFailure::InputStateEpochMismatch: return "InputStateEpochMismatch";
            case CurrentCycleGateFailure::GamepadSessionMismatch: return "GamepadSessionMismatch";
            case CurrentCycleGateFailure::ContextMismatch: return "ContextMismatch";
            case CurrentCycleGateFailure::ControlMapMismatch: return "ControlMapMismatch";
            case CurrentCycleGateFailure::CutoffMismatch: return "CutoffMismatch";
            case CurrentCycleGateFailure::PhysicalFactsIncomplete: return "PhysicalFactsIncomplete";
            case CurrentCycleGateFailure::RouteUnavailable: return "RouteUnavailable";
            case CurrentCycleGateFailure::AdapterFailure: return "AdapterFailure";
            case CurrentCycleGateFailure::ConsumerOrderUnproven: return "ConsumerOrderUnproven";
            case CurrentCycleGateFailure::ScratchCapacityExceeded: return "ScratchCapacityExceeded";
            default: return "Unknown";
            }
        }

        const char* AdapterResult(const MixedInputEvidenceRecord& record)
        {
            if (!record.audit.success ||
                record.audit.failure != gameplay::CurrentCycleGateFailure::None) {
                return GateFailureName(record.audit.failure);
            }
            if (record.plan.requiresEventMutation && record.audit.shadowOnly) {
                return "ShadowOnly";
            }
            return "Success";
        }

        std::string LedgerSummary(const gameplay::CurrentCycleSensitiveState& state)
        {
            std::ostringstream value;
            value << state.revision << '/'
                  << static_cast<unsigned int>(state.sprint.activeSourceMask) << '/'
                  << state.sprint.lastReleaseToken;
            return value.str();
        }

        std::string MaterializationId(const input::PollMaterializationReceipt& receipt)
        {
            std::ostringstream value;
            value << receipt.hookCallSequence << ':'
                  << receipt.identity.publicationGeneration << ':'
                  << receipt.identity.runtimeGeneration << ':'
                  << receipt.identity.packetNumber;
            return value.str();
        }

        std::string DecisionKey(const MixedInputEvidenceRecord& record)
        {
            std::ostringstream key;
            key << record.currentInputStateEpoch << ':'
                << record.currentGamepadSessionId << ':'
                << static_cast<unsigned int>(record.receiptFailure) << ':'
                << Bool(record.receipt.has_value()) << ':'
                << static_cast<unsigned int>(record.plan.failure) << ':'
                << static_cast<unsigned int>(record.plan.affectedChannels) << ':'
                << Bool(record.plan.requiresEventMutation) << ':'
                << Bool(record.plan.commitCurrentCycleSensitiveState) << ':'
                << static_cast<unsigned int>(record.plan.currentEventWriterCount) << ':'
                << static_cast<unsigned int>(record.plan.nextPollWriterCount) << ':'
                << Bool(record.audit.success) << ':'
                << Bool(record.audit.shadowOnly) << ':'
                << Bool(record.audit.mutationApplied) << ':'
                << static_cast<unsigned int>(record.audit.failure) << ':'
                << record.audit.wouldMutateCount << ':'
                << static_cast<unsigned int>(record.sprintDecision.winningPressSource) << ':'
                << static_cast<unsigned int>(
                    record.sprintDecision.joiningPressSuppressionMask) << ':'
                << static_cast<unsigned int>(
                    record.sprintDecision.nonFinalReleaseSuppressionMask) << ':'
                << Bool(record.sprintDecision.virtualBridgeDesired) << ':'
                << Bool(record.sprintDecision.finalRelease) << ':'
                << static_cast<unsigned int>(record.before.sprint.activeSourceMask) << ':'
                << static_cast<unsigned int>(record.after.sprint.activeSourceMask) << ':'
                << Bool(record.before.sprint.virtualMaterialized) << ':'
                << Bool(record.after.sprint.virtualMaterialized) << ':'
                << Bool(record.before.revision != record.after.revision) << ':'
                << Bool(record.commit.committed) << ':'
                << Bool(record.commit.alreadyConsumed) << ':'
                << Bool(record.commit.invalidToken) << ':'
                << static_cast<unsigned int>(record.commit.failClosedChannels);
            return key.str();
        }

        const char* SustainedSourceName(gameplay::SustainedContributorBit source)
        {
            using gameplay::SustainedContributorBit;
            switch (source) {
            case SustainedContributorBit::Gamepad: return "Gamepad";
            case SustainedContributorBit::KeyboardPhysical: return "KeyboardPhysical";
            case SustainedContributorBit::MousePhysical: return "MousePhysical";
            default: return "None";
            }
        }

        const char* CursorOwnerName(presentation::CursorOwner owner)
        {
            return owner == presentation::CursorOwner::Gamepad ?
                "Gamepad" : "KeyboardMouse";
        }

        const char* PresentationOwnerName(presentation::PresentationOwner owner)
        {
            return owner == presentation::PresentationOwner::Gamepad ?
                "Gamepad" : "KeyboardMouse";
        }

        const char* NavigationOwnerName(presentation::NavigationOwner owner)
        {
            using presentation::NavigationOwner;
            switch (owner) {
            case NavigationOwner::KeyboardMouse: return "KeyboardMouse";
            case NavigationOwner::Gamepad: return "Gamepad";
            default: return "None";
            }
        }

        const char* CursorFailureName(presentation::CursorHandoffFailure failure)
        {
            using presentation::CursorHandoffFailure;
            switch (failure) {
            case CursorHandoffFailure::None: return "None";
            case CursorHandoffFailure::MenuIdentityMismatch: return "MenuIdentityMismatch";
            case CursorHandoffFailure::MovieIdentityMismatch: return "MovieIdentityMismatch";
            case CursorHandoffFailure::MappingUnverified: return "MappingUnverified";
            case CursorHandoffFailure::PositionUnavailable: return "PositionUnavailable";
            case CursorHandoffFailure::CoordinateOutOfRange: return "CoordinateOutOfRange";
            case CursorHandoffFailure::WriteVerificationFailed: return "WriteVerificationFailed";
            default: return "Unknown";
            }
        }

        const presentation::CursorHandoffPlan* EvidencePlan(
            const PresentationEvidenceRecord& record)
        {
            if (record.ack && record.planBefore) {
                return &*record.planBefore;
            }
            return record.planAfter ? &*record.planAfter : nullptr;
        }

        std::size_t CountNonOriginalEngineDomains(
            const gameplay::EngineModeDecisionSnapshot& snapshot)
        {
            return static_cast<std::size_t>(std::count_if(
                snapshot.byDomain.begin(),
                snapshot.byDomain.end(),
                [](const auto& decision) {
                    return decision.mode != gameplay::EngineInputMode::Original;
                }));
        }

        std::size_t CountOriginalOnlyEngineDomains(
            const gameplay::EngineModeDecisionSnapshot& snapshot)
        {
            return static_cast<std::size_t>(std::count_if(
                snapshot.byDomain.begin(),
                snapshot.byDomain.end(),
                [](const auto& decision) {
                    return decision.causality == gameplay::EngineDecisionCausality::OriginalOnly;
                }));
        }

        std::string PresentationDecisionKey(const PresentationEvidenceRecord& record)
        {
            std::ostringstream key;
            const auto* plan = EvidencePlan(record);
            key << static_cast<unsigned int>(record.after.cursor.requestedOwner) << ':'
                << static_cast<unsigned int>(record.after.cursor.committedOwner) << ':'
                << record.after.cursor.pendingToken << ':'
                << Bool(record.after.cursor.positionSyncRequired) << ':'
                << static_cast<unsigned int>(record.after.cursor.reason) << ':'
                << static_cast<unsigned int>(record.after.menu.owner) << ':'
                << static_cast<unsigned int>(record.after.menu.navigationOwner) << ':'
                << record.after.contextRevision << ':'
                << record.after.presentationEpoch << ':'
                << record.after.targetMenuInstanceId << ':'
                << Bool(plan != nullptr) << ':'
                << (plan ? plan->token : 0) << ':'
                << Bool(record.ack.has_value()) << ':'
                << (record.ack ? record.ack->token : 0) << ':'
                << (record.ack ? record.ack->contextRevision : 0) << ':'
                << (record.ack ? record.ack->presentationEpoch : 0) << ':'
                << (record.ack ? record.ack->targetMenuInstanceId : 0) << ':'
                << Bool(record.ack && record.ack->positionSynchronized) << ':'
                << (record.ack ? static_cast<unsigned int>(record.ack->failure) : 0) << ':'
                << Bool(record.engineSnapshotCurrent) << ':'
                << CountNonOriginalEngineDomains(record.engine);
            return key.str();
        }
    }

    std::string SerializeMixedInputEvidenceJsonLine(const MixedInputEvidenceRecord& record)
    {
        using gameplay::CurrentCycleChannel;
        const auto writer = [&](CurrentCycleChannel channel, bool currentCycle) {
            if (!HasChannel(record.plan.affectedChannels, channel)) {
                return 0U;
            }
            return static_cast<unsigned int>(currentCycle ?
                record.plan.currentEventWriterCount : record.plan.nextPollWriterCount);
        };
        const auto receiptCount = record.receipt ? 1U : 0U;
        const auto ambiguous = record.receiptFailure == input::PollReceiptConsumeFailure::Ambiguous;
        const auto reused = record.receiptFailure == input::PollReceiptConsumeFailure::AlreadyConsumed;
        const auto planEpoch = record.receipt ? record.receipt->identity.inputStateEpoch : 0;
        const auto planSession = record.receipt ? record.receipt->identity.gamepadSessionId : 0;
        const auto releaseDelta = record.after.sprint.lastReleaseToken >=
                record.before.sprint.lastReleaseToken ?
            record.after.sprint.lastReleaseToken - record.before.sprint.lastReleaseToken : 0;

        std::ostringstream json;
        json << "{\"schemaVersion\":1"
             << ",\"caseId\":\"runtime-shadow\""
             << ",\"monotonicUs\":" << record.monotonicUs
             << ",\"ownerTickToken\":" << record.ownerTickToken
             << ",\"writers\":{\"currentCycle\":{"
             << "\"Look\":" << writer(CurrentCycleChannel::Look, true)
             << ",\"Move\":" << writer(CurrentCycleChannel::Move, true)
             << ",\"Combat\":" << writer(CurrentCycleChannel::Combat, true)
             << ",\"TransientDigital\":" << writer(CurrentCycleChannel::TransientDigital, true)
             << "},\"nextPoll\":{"
             << "\"Look\":" << writer(CurrentCycleChannel::Look, false)
             << ",\"Move\":" << writer(CurrentCycleChannel::Move, false)
             << ",\"Combat\":" << writer(CurrentCycleChannel::Combat, false)
             << ",\"TransientDigital\":" << writer(CurrentCycleChannel::TransientDigital, false)
             << "}}"
             << ",\"sprint\":{\"previousSourceMask\":"
             << static_cast<unsigned int>(record.before.sprint.activeSourceMask)
             << ",\"sourceMask\":" << static_cast<unsigned int>(record.after.sprint.activeSourceMask)
             << ",\"aggregateHeld\":" << Bool(record.after.sprint.activeSourceMask != 0)
             << ",\"virtualHeld\":" << Bool(record.after.sprint.virtualMaterialized)
             << ",\"virtualBridgeDesired\":"
             << Bool(record.sprintDecision.virtualBridgeDesired)
             << ",\"winningPressSource\":\""
             << SustainedSourceName(record.sprintDecision.winningPressSource) << "\""
             << ",\"joiningPressSuppressionMask\":"
             << static_cast<unsigned int>(
                    record.sprintDecision.joiningPressSuppressionMask)
             << ",\"nonFinalReleaseSuppressionMask\":"
             << static_cast<unsigned int>(
                    record.sprintDecision.nonFinalReleaseSuppressionMask)
             << ",\"requiresCurrentCycleMutation\":"
             << Bool(record.sprintDecision.requiresCurrentCycleMutation)
             << ",\"finalRelease\":" << Bool(record.sprintDecision.finalRelease)
             << ",\"releaseToken\":" << record.sprintDecision.releaseToken
             << ",\"releaseTokenDelta\":" << releaseDelta << '}'
             << ",\"apply\":{\"attempted\":" << Bool(record.receipt.has_value())
             << ",\"planEpoch\":" << planEpoch
             << ",\"currentEpoch\":" << record.currentInputStateEpoch
             << ",\"planGamepadSession\":" << planSession
             << ",\"currentGamepadSession\":" << record.currentGamepadSessionId << '}'
             << ",\"currentCycleMutation\":{\"required\":"
             << Bool(record.plan.requiresEventMutation)
             << ",\"applied\":" << Bool(record.audit.mutationApplied)
             << ",\"pollReceipt\":{\"count\":" << receiptCount
             << ",\"materializationId\":\""
             << (record.receipt ? MaterializationId(*record.receipt) : std::string{}) << "\""
             << ",\"ambiguous\":" << Bool(ambiguous)
             << ",\"reused\":" << Bool(reused) << "}}"
             << ",\"adapter\":{\"result\":\"" << AdapterResult(record) << "\""
             << ",\"sensitiveLedgerBefore\":\"" << LedgerSummary(record.before) << "\""
             << ",\"sensitiveLedgerAfter\":\"" << LedgerSummary(record.after) << "\"}"
             << ",\"commit\":{\"committed\":" << Bool(record.commit.committed)
             << ",\"alreadyConsumed\":" << Bool(record.commit.alreadyConsumed)
             << ",\"invalidToken\":" << Bool(record.commit.invalidToken)
             << ",\"failClosedChannels\":"
             << static_cast<unsigned int>(record.commit.failClosedChannels) << "}}";
        return json.str();
    }

    std::string SerializePresentationEvidenceJsonLine(
        const PresentationEvidenceRecord& record)
    {
        const auto* plan = EvidencePlan(record);
        const bool commitChanged =
            record.before.cursor.committedOwner != record.after.cursor.committedOwner;
        const bool positionSyncRequired = record.ack ?
            record.before.cursor.positionSyncRequired :
            record.after.cursor.positionSyncRequired;
        const bool ackSuccess = record.ack &&
            record.ack->failure == presentation::CursorHandoffFailure::None;
        const auto nonOriginalDomains = CountNonOriginalEngineDomains(record.engine);
        const auto originalOnlyDomains = CountOriginalOnlyEngineDomains(record.engine);
        const auto& engineIdentity = record.engine.byDomain.front();

        std::ostringstream json;
        json << "{\"schemaVersion\":1"
             << ",\"caseId\":\"runtime-presentation-shadow\""
             << ",\"monotonicUs\":" << record.monotonicUs
             << ",\"ownerTickToken\":" << record.ownerTickToken
             << ",\"cursor\":{\"requestedOwner\":\""
             << CursorOwnerName(record.after.cursor.requestedOwner) << "\""
             << ",\"previousCommittedOwner\":\""
             << CursorOwnerName(record.before.cursor.committedOwner) << "\""
             << ",\"committedOwner\":\""
             << CursorOwnerName(record.after.cursor.committedOwner) << "\""
             << ",\"commitChanged\":" << Bool(commitChanged)
             << ",\"pendingAfter\":" << Bool(record.after.cursor.pendingToken != 0)
             << ",\"plan\":{\"token\":" << (plan ? plan->token : 0)
             << ",\"contextRevision\":" << (plan ? plan->contextRevision : 0)
             << ",\"presentationEpoch\":" << (plan ? plan->presentationEpoch : 0)
             << ",\"menuInstance\":" << (plan ? plan->targetMenuInstanceId : 0)
             << ",\"menuPtr\":" << (plan ? plan->targetMenuPtr : 0)
             << ",\"moviePtr\":" << (plan ? plan->targetMoviePtr : 0)
             << ",\"from\":\""
             << (plan ? CursorOwnerName(plan->from) : "KeyboardMouse") << "\""
             << ",\"to\":\""
             << (plan ? CursorOwnerName(plan->to) : "KeyboardMouse") << "\""
             << ",\"positionSyncRequired\":" << Bool(positionSyncRequired) << '}'
             << ",\"ack\":{\"success\":" << Bool(ackSuccess)
             << ",\"token\":" << (record.ack ? record.ack->token : 0)
             << ",\"contextRevision\":" << (record.ack ? record.ack->contextRevision : 0)
             << ",\"presentationEpoch\":" << (record.ack ? record.ack->presentationEpoch : 0)
             << ",\"menuInstance\":" << (record.ack ? record.ack->targetMenuInstanceId : 0)
             << ",\"positionSyncApplied\":"
             << Bool(record.ack && record.ack->positionSynchronized)
             << ",\"failure\":\""
             << (record.ack ? CursorFailureName(record.ack->failure) : "None") << "\"}}"
             << ",\"menu\":{\"owner\":\""
             << PresentationOwnerName(record.after.menu.owner) << "\""
             << ",\"navigationOwner\":\""
             << NavigationOwnerName(record.after.menu.navigationOwner) << "\""
             << ",\"contextRevision\":" << record.after.contextRevision
             << ",\"presentationEpoch\":" << record.after.presentationEpoch
             << ",\"menuInstance\":" << record.after.targetMenuInstanceId
             << ",\"menuPtr\":" << record.after.targetMenuPtr
             << ",\"moviePtr\":" << record.after.targetMenuMoviePtr
             << ",\"uiContext\":"
             << static_cast<unsigned int>(record.after.uiContextId) << '}'
             << ",\"engineQuery\":{\"overrideApplied\":"
             << "false"
             << ",\"scopeActive\":false"
             << ",\"domain\":\"Unknown\""
             << ",\"callerVerified\":false"
             << ",\"snapshotCurrent\":" << Bool(record.engineSnapshotCurrent)
             << ",\"snapshotGeneration\":" << record.engine.generation
             << ",\"ownerTickToken\":" << engineIdentity.ownerTickToken
             << ",\"inputStateEpoch\":" << engineIdentity.inputStateEpoch
             << ",\"contextRevision\":" << engineIdentity.contextRevision
             << ",\"runtimeGeneration\":" << engineIdentity.runtimeGeneration
             << ",\"originalOnlyDomainCount\":" << originalOnlyDomains
             << ",\"nonOriginalDomainCount\":" << nonOriginalDomains << "}}";
        return json.str();
    }

    bool MixedInputEvidenceSampler::ShouldEmit(const MixedInputEvidenceRecord& record)
    {
        return ShouldEmitDecision(DecisionKey(record), record.monotonicUs);
    }

    bool MixedInputEvidenceSampler::ShouldEmitDecision(
        std::string key,
        std::uint64_t monotonicUs)
    {
        const bool first = _lastDecisionKey.empty();
        const bool changed = !first && key != _lastDecisionKey;
        const bool clockRegressed = !first && monotonicUs < _lastEmitUs;
        const bool healthDue = !first && !clockRegressed &&
            monotonicUs - _lastEmitUs >= kHealthIntervalUs;
        if (!first && !changed && !clockRegressed && !healthDue) {
            return false;
        }
        _lastDecisionKey = std::move(key);
        _lastEmitUs = monotonicUs;
        return true;
    }

    void MixedInputEvidenceSampler::Reset()
    {
        _lastDecisionKey.clear();
        _lastEmitUs = 0;
    }

    MixedInputEvidenceRecorder& MixedInputEvidenceRecorder::GetSingleton()
    {
        static MixedInputEvidenceRecorder recorder;
        return recorder;
    }

    void MixedInputEvidenceRecorder::ActivateSessionLocked(
        const std::filesystem::path& root,
        std::string_view session)
    {
        if (_activeRoot == root && _activeSession == session) {
            return;
        }
        _activeRoot = root;
        _activeSession = session;
        _sampler.Reset();
        _presentationSampler.Reset();
    }

    bool MixedInputEvidenceRecorder::AppendLineLocked(std::string_view line)
    {
        const auto directory = _activeRoot / _activeSession;
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return false;
        }
        std::ofstream output(directory / "mixed_input_evidence.jsonl", std::ios::app);
        if (!output.is_open()) {
            return false;
        }
        output << line << '\n';
        output.flush();
        return output.good();
    }

    void MixedInputEvidenceRecorder::Record(const MixedInputEvidenceRecord& record)
    {
        std::scoped_lock lock(_mutex);
        const auto& config = input::RuntimeConfig::GetSingleton();
        if (!config.EnableTraceRecording()) {
            return;
        }

        const auto root = config.TraceOutputDir();
        const auto session = std::string(config.TraceSession());
        ActivateSessionLocked(root, session);
        if (!_sampler.ShouldEmit(record)) {
            return;
        }

        if (!AppendLineLocked(SerializeMixedInputEvidenceJsonLine(record))) {
            _sampler.Reset();
        }
    }

    void MixedInputEvidenceRecorder::RecordPresentation(
        const PresentationEvidenceRecord& record)
    {
        std::scoped_lock lock(_mutex);
        const auto& config = input::RuntimeConfig::GetSingleton();
        if (!config.EnableTraceRecording()) {
            return;
        }

        const auto root = config.TraceOutputDir();
        const auto session = std::string(config.TraceSession());
        ActivateSessionLocked(root, session);
        if (!_presentationSampler.ShouldEmitDecision(
                PresentationDecisionKey(record),
                record.monotonicUs)) {
            return;
        }
        if (!AppendLineLocked(SerializePresentationEvidenceJsonLine(record))) {
            _presentationSampler.Reset();
        }
    }

    void MixedInputEvidenceRecorder::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _sampler.Reset();
        _presentationSampler.Reset();
        _activeRoot.clear();
        _activeSession.clear();
    }
}
