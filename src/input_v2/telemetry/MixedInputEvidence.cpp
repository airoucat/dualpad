#include "pch.h"

#include "input_v2/telemetry/MixedInputEvidence.h"

#include "input/RuntimeConfig.h"

#include <fstream>
#include <sstream>

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

    bool MixedInputEvidenceSampler::ShouldEmit(const MixedInputEvidenceRecord& record)
    {
        const auto key = DecisionKey(record);
        const bool first = _lastDecisionKey.empty();
        const bool changed = !first && key != _lastDecisionKey;
        const bool clockRegressed = !first && record.monotonicUs < _lastEmitUs;
        const bool healthDue = !first && !clockRegressed &&
            record.monotonicUs - _lastEmitUs >= kHealthIntervalUs;
        if (!first && !changed && !clockRegressed && !healthDue) {
            return false;
        }
        _lastDecisionKey = key;
        _lastEmitUs = record.monotonicUs;
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

    void MixedInputEvidenceRecorder::Record(const MixedInputEvidenceRecord& record)
    {
        std::scoped_lock lock(_mutex);
        const auto& config = input::RuntimeConfig::GetSingleton();
        if (!config.EnableTraceRecording()) {
            return;
        }

        const auto root = config.TraceOutputDir();
        const auto session = std::string(config.TraceSession());
        if (_activeRoot != root || _activeSession != session) {
            _activeRoot = root;
            _activeSession = session;
            _sampler.Reset();
        }
        if (!_sampler.ShouldEmit(record)) {
            return;
        }

        const auto directory = _activeRoot / _activeSession;
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return;
        }
        std::ofstream output(directory / "mixed_input_evidence.jsonl", std::ios::app);
        output << SerializeMixedInputEvidenceJsonLine(record) << '\n';
    }

    void MixedInputEvidenceRecorder::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _sampler.Reset();
        _activeRoot.clear();
        _activeSession.clear();
    }
}
