#include "pch.h"

#include "input/injection/PollMaterializationReceipt.h"

#include <algorithm>

namespace dualpad::input
{
    PollFrameIdentity MakePollFrameIdentity(
        const input_v2::gameplay::PollOutputFrame& frame) noexcept
    {
        return PollFrameIdentity{
            .publicationGeneration = frame.publicationGeneration,
            .runtimeGeneration = frame.runtimeGeneration,
            .packetNumber = frame.packetNumber,
            .inputStateEpoch = frame.inputStateEpoch,
            .gamepadSessionId = frame.gamepadSessionId,
            .contextRevision = static_cast<std::uint32_t>(frame.contextRevision),
            .controlMapRevision = frame.controlMapRevision,
            .orderedCutoffSeq = frame.orderedCutoffSeq,
            .eventBatchToken = frame.eventBatchToken
        };
    }

    PollMaterializationReceiptStore& PollMaterializationReceiptStore::GetSingleton()
    {
        static PollMaterializationReceiptStore store;
        return store;
    }

    std::optional<PollMaterializationReceipt>
    PollMaterializationReceiptStore::PublishAfterSuccessfulSerialize(
        const input_v2::gameplay::PollOutputFrame& frame,
        std::uint32_t threadId)
    {
        std::scoped_lock lock(_mutex);
        PollMaterializationReceipt receipt{
            .hookCallSequence = ++_nextHookCallSequence,
            .threadId = threadId,
            .identity = MakePollFrameIdentity(frame),
            .serializeSucceeded = true,
            .routeAvailable = frame.routeHealth == input_v2::gameplay::PollOutputRouteHealth::Ready,
            .materializedLookEvent = frame.rx != 0 || frame.ry != 0,
            .materializedMoveEvent = frame.lx != 0 || frame.ly != 0,
            .materializedCombatEvent = frame.lt != 0 || frame.rt != 0,
            .materializedTransientEvent = frame.buttons != 0
        };
        return PublishLocked(receipt) ? std::optional{ receipt } : std::nullopt;
    }

    bool PollMaterializationReceiptStore::PublishForTests(PollMaterializationReceipt receipt)
    {
        std::scoped_lock lock(_mutex);
        _nextHookCallSequence = std::max(_nextHookCallSequence, receipt.hookCallSequence);
        return PublishLocked(std::move(receipt));
    }

    bool PollMaterializationReceiptStore::PublishLocked(PollMaterializationReceipt receipt)
    {
        if (!receipt.serializeSucceeded || receipt.hookCallSequence == 0 || receipt.threadId == 0) {
            return false;
        }
        if (_slots.size() == kCapacity) {
            _slots.pop_front();
        }
        _slots.push_back(Slot{ .receipt = std::move(receipt) });
        return true;
    }

    PollReceiptConsumeResult PollMaterializationReceiptStore::ConsumeExact(
        std::uint64_t hookCallSequence,
        std::uint32_t threadId)
    {
        std::scoped_lock lock(_mutex);
        const auto found = std::find_if(_slots.begin(), _slots.end(), [&](const Slot& slot) {
            return slot.receipt.hookCallSequence == hookCallSequence;
        });
        if (found == _slots.end()) {
            return {};
        }
        if (found->receipt.threadId != threadId) {
            return { .failure = PollReceiptConsumeFailure::ThreadMismatch };
        }
        if (found->consumed) {
            return { .failure = PollReceiptConsumeFailure::AlreadyConsumed };
        }
        found->consumed = true;
        return { .receipt = found->receipt, .failure = PollReceiptConsumeFailure::None };
    }

    PollReceiptConsumeResult PollMaterializationReceiptStore::ConsumeForThread(
        std::uint32_t threadId)
    {
        std::scoped_lock lock(_mutex);
        Slot* candidate = nullptr;
        bool consumedForThread = false;
        bool unconsumedOtherThread = false;
        for (auto& slot : _slots) {
            if (slot.receipt.threadId == threadId) {
                if (slot.consumed) {
                    consumedForThread = true;
                    continue;
                }
                if (candidate) {
                    return { .failure = PollReceiptConsumeFailure::Ambiguous };
                }
                candidate = &slot;
            } else if (!slot.consumed) {
                unconsumedOtherThread = true;
            }
        }
        if (candidate) {
            candidate->consumed = true;
            return { .receipt = candidate->receipt, .failure = PollReceiptConsumeFailure::None };
        }
        if (unconsumedOtherThread) {
            return { .failure = PollReceiptConsumeFailure::ThreadMismatch };
        }
        return { .failure = consumedForThread ?
            PollReceiptConsumeFailure::AlreadyConsumed :
            PollReceiptConsumeFailure::Missing };
    }

    void PollMaterializationReceiptStore::Reset()
    {
        std::scoped_lock lock(_mutex);
        _slots.clear();
        _nextHookCallSequence = 0;
    }

    void PollMaterializationReceiptStore::ResetForTests()
    {
        Reset();
    }
}
