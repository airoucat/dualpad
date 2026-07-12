#include "pch.h"

#include "input_v2/presentation/CursorHandoffAckMailbox.h"

#include <utility>

namespace dualpad::input_v2::presentation
{
    CursorHandoffAckMailbox& CursorHandoffAckMailbox::GetSingleton()
    {
        static CursorHandoffAckMailbox mailbox;
        return mailbox;
    }

    void CursorHandoffAckMailbox::PublishFromUiTask(CursorHandoffAck ack)
    {
        std::scoped_lock lock(_mutex);
        if (_pending.size() == kCapacity) {
            _pending.pop_front();
        }
        _pending.push_back(CursorHandoffAckEnvelope{
            .ackSequence = _nextAckSequence++,
            .ack = std::move(ack)
        });
    }

    std::optional<CursorHandoffAckEnvelope> CursorHandoffAckMailbox::ConsumeExactOnOwnerTick(
        std::uint64_t token,
        std::uint32_t contextRevision,
        std::uint32_t presentationEpoch,
        std::uint64_t targetMenuInstanceId)
    {
        std::scoped_lock lock(_mutex);
        std::optional<CursorHandoffAckEnvelope> exact;
        while (!_pending.empty()) {
            auto envelope = std::move(_pending.front());
            _pending.pop_front();
            const auto& ack = envelope.ack;
            if (!exact && ack.token == token &&
                ack.contextRevision == contextRevision &&
                ack.presentationEpoch == presentationEpoch &&
                ack.targetMenuInstanceId == targetMenuInstanceId) {
                exact = std::move(envelope);
            }
        }
        return exact;
    }

    void CursorHandoffAckMailbox::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _pending.clear();
        _nextAckSequence = 1;
    }
}
