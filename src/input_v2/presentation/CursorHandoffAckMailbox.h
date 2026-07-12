#pragma once

#include "input_v2/presentation/PresentationProjection.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace dualpad::input_v2::presentation
{
    struct CursorHandoffAckEnvelope
    {
        std::uint64_t ackSequence{ 0 };
        CursorHandoffAck ack{};
    };

    class CursorHandoffAckMailbox
    {
    public:
        static CursorHandoffAckMailbox& GetSingleton();

        void PublishFromUiTask(CursorHandoffAck ack);
        [[nodiscard]] std::optional<CursorHandoffAckEnvelope> ConsumeExactOnOwnerTick(
            std::uint64_t token,
            std::uint32_t contextRevision,
            std::uint32_t presentationEpoch,
            std::uint64_t targetMenuInstanceId);
        void ResetForTests();

    private:
        static constexpr std::size_t kCapacity = 16;
        std::mutex _mutex;
        std::deque<CursorHandoffAckEnvelope> _pending;
        std::uint64_t _nextAckSequence{ 1 };
    };
}
