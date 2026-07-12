#pragma once

#include "input/injection/PollMaterializationReceipt.h"
#include "input_v2/gameplay/RuntimeInputPublication.h"
#include "input_v2/presentation/PresentationProjection.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace dualpad::input_v2::telemetry
{
    struct MixedInputEvidenceRecord
    {
        std::uint64_t monotonicUs{ 0 };
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t currentInputStateEpoch{ 0 };
        std::uint64_t currentGamepadSessionId{ 0 };
        input::PollReceiptConsumeFailure receiptFailure{ input::PollReceiptConsumeFailure::None };
        std::optional<input::PollMaterializationReceipt> receipt;
        gameplay::CurrentCycleGatePlan plan{};
        gameplay::CurrentCycleAdapterAudit audit{};
        gameplay::SustainedContributorDecision sprintDecision{};
        gameplay::CurrentCycleSensitiveState before{};
        gameplay::CurrentCycleSensitiveState after{};
        gameplay::RuntimeInputCommitResult commit{};
    };

    std::string SerializeMixedInputEvidenceJsonLine(const MixedInputEvidenceRecord& record);

    struct PresentationEvidenceRecord
    {
        std::uint64_t monotonicUs{ 0 };
        std::uint64_t ownerTickToken{ 0 };
        presentation::PublishedPresentationState before{};
        presentation::PublishedPresentationState after{};
        std::optional<presentation::CursorHandoffPlan> planBefore;
        std::optional<presentation::CursorHandoffPlan> planAfter;
        std::optional<presentation::CursorHandoffAck> ack;
        gameplay::EngineModeDecisionSnapshot engine{};
        bool engineSnapshotCurrent{ false };
    };

    std::string SerializePresentationEvidenceJsonLine(
        const PresentationEvidenceRecord& record);

    class MixedInputEvidenceSampler
    {
    public:
        bool ShouldEmit(const MixedInputEvidenceRecord& record);
        bool ShouldEmitDecision(std::string decisionKey, std::uint64_t monotonicUs);
        void Reset();

    private:
        static constexpr std::uint64_t kHealthIntervalUs = 10'000'000;
        std::string _lastDecisionKey;
        std::uint64_t _lastEmitUs{ 0 };
    };

    class MixedInputEvidenceRecorder
    {
    public:
        static MixedInputEvidenceRecorder& GetSingleton();

        void Record(const MixedInputEvidenceRecord& record);
        void RecordPresentation(const PresentationEvidenceRecord& record);
        void ResetForTests();

    private:
        void ActivateSessionLocked(
            const std::filesystem::path& root,
            std::string_view session);
        bool AppendLineLocked(std::string_view line);

        std::mutex _mutex;
        MixedInputEvidenceSampler _sampler;
        MixedInputEvidenceSampler _presentationSampler;
        std::filesystem::path _activeRoot;
        std::string _activeSession;
    };
}
