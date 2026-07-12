#pragma once

#include "input_v2/gameplay/PollOutputFrame.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace dualpad::input
{
    struct PollFrameIdentity
    {
        std::uint64_t publicationGeneration{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
        std::uint32_t packetNumber{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        std::uint64_t orderedCutoffSeq{ 0 };
        std::uint64_t eventBatchToken{ 0 };

        friend bool operator==(const PollFrameIdentity&, const PollFrameIdentity&) = default;
    };

    PollFrameIdentity MakePollFrameIdentity(
        const input_v2::gameplay::PollOutputFrame& frame) noexcept;

    struct PollMaterializationReceipt
    {
        std::uint64_t hookCallSequence{ 0 };
        std::uint32_t threadId{ 0 };
        PollFrameIdentity identity{};
        bool serializeSucceeded{ false };
        bool routeAvailable{ false };
        bool materializedLookEvent{ false };
        bool materializedMoveEvent{ false };
        bool materializedCombatEvent{ false };
        bool materializedTransientEvent{ false };
    };

    enum class PollReceiptConsumeFailure : std::uint8_t
    {
        None = 0,
        Missing,
        Ambiguous,
        AlreadyConsumed,
        ThreadMismatch
    };

    struct PollReceiptConsumeResult
    {
        std::optional<PollMaterializationReceipt> receipt;
        PollReceiptConsumeFailure failure{ PollReceiptConsumeFailure::Missing };

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return receipt.has_value() && failure == PollReceiptConsumeFailure::None;
        }
    };

    class PollMaterializationReceiptStore
    {
    public:
        static PollMaterializationReceiptStore& GetSingleton();

        std::optional<PollMaterializationReceipt> PublishAfterSuccessfulSerialize(
            const input_v2::gameplay::PollOutputFrame& frame,
            std::uint32_t threadId);
        bool PublishForTests(PollMaterializationReceipt receipt);
        PollReceiptConsumeResult ConsumeExact(
            std::uint64_t hookCallSequence,
            std::uint32_t threadId);
        PollReceiptConsumeResult ConsumeForThread(std::uint32_t threadId);
        void Reset();
        void ResetForTests();

    private:
        struct Slot
        {
            PollMaterializationReceipt receipt{};
            bool consumed{ false };
        };

        static constexpr std::size_t kCapacity = 64;
        bool PublishLocked(PollMaterializationReceipt receipt);

        std::mutex _mutex;
        std::deque<Slot> _slots;
        std::uint64_t _nextHookCallSequence{ 0 };
    };
}
