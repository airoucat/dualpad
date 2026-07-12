#pragma once

#include "input/injection/PollMaterializationReceipt.h"
#include "input_v2/gameplay/RuntimeInputPublication.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

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
        gameplay::CurrentCycleSensitiveState before{};
        gameplay::CurrentCycleSensitiveState after{};
        gameplay::RuntimeInputCommitResult commit{};
    };

    std::string SerializeMixedInputEvidenceJsonLine(const MixedInputEvidenceRecord& record);

    class MixedInputEvidenceSampler
    {
    public:
        bool ShouldEmit(const MixedInputEvidenceRecord& record);
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
        void ResetForTests();

    private:
        std::mutex _mutex;
        MixedInputEvidenceSampler _sampler;
        std::filesystem::path _activeRoot;
        std::string _activeSession;
    };
}
