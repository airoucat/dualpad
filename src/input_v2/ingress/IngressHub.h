#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input_v2/ingress/IngressMarkers.h"
#include "input_v2/ingress/LatestPadState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace dualpad::input_v2::ingress
{
    struct PublishedIngressBatchReceipt
    {
        bool accepted{ false };
        InputResetReasonMask publishedResetReasons{ 0 };
        InputResetScope publishedResetScope{ InputResetScope::GlobalInputState };
        std::uint64_t firstOrderedSeq{ 0 };
        std::uint64_t causalOrderedTailSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    struct IngressBoundaryObservation
    {
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint64_t controlMapFingerprint{ 0 };
        std::uint64_t bindingGeneration{ 0 };
    };

    struct OwnerKbmIngressDraft
    {
        IngressBoundaryObservation boundary{};
        KbmGameplayIngressBatchDraft kbm{};
    };

    struct IngressCapture
    {
        std::uint64_t generation{ 0 };
        std::vector<IngressEvent> events;
        std::optional<LatestPadState> latestPadState;
        std::optional<LatestSourceEvidence> latestSourceEvidence;
        std::size_t remainingEvents{ 0 };
        std::uint64_t orderedCutoffSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::optional<GamepadConnectionFacts> latestGamepadConnection;
        std::optional<LatestKbmGameplayFacts> latestKbmGameplay;
    };

    class IngressHub
    {
    public:
        explicit IngressHub(std::size_t capacity = 256);

        static IngressHub& GetSingleton();

        bool PushEvent(IngressEvent event);
        bool PushEvents(std::vector<IngressEvent> events);
        bool PushPadSnapshot(
            const dualpad::input::PadEventSnapshot& snapshot,
            bool retainLegacySnapshot = true,
            const presentation::SourceEvidenceFrame* sourceEvidenceFrame = nullptr);
        PublishedIngressBatchReceipt PublishOwnerKbmBatch(OwnerKbmIngressDraft batch);
        PublishedIngressBatchReceipt PublishGamepadBatch(
            ClassifiedGamepadReportDraft report,
            std::optional<GamepadConnectionDraft> connection);
        PublishedIngressBatchReceipt PublishGlobalReset(
            InputResetReasonMask reasons,
            InputResetScope scope);
        PublishedIngressBatchReceipt PublishGamepadDisconnect();
        void PublishSourceEvidenceFrame(const presentation::SourceEvidenceFrame& frame);
        void PushManifestEpochChanged(std::uint64_t manifestEpoch);
        void PushSequenceGap();
        void PushExplicitReset();
        std::vector<IngressEvent> Drain();
        std::vector<IngressEvent> Drain(std::size_t maxEvents);
        IngressCapture Capture(std::size_t maxEvents);
        bool HasUncapturedLatest() const;
        std::size_t PendingCount() const;
        std::size_t PendingLegacySnapshotCount() const;
        void ResetForTests();

    private:
        std::uint64_t NextSeqLocked();
        std::uint64_t NowMonotonicUs() const;
        bool PushLocked(IngressEvent event);
        std::vector<IngressEvent> DrainLocked(std::size_t maxEvents);
        void ApplySourceEvidenceFrameLocked(
            const presentation::SourceEvidenceFrame& frame,
            std::vector<IngressEvent>& orderedEvents);
        void ReplaceBacklogWithOverflowLocked(
            std::uint64_t seq,
            std::uint64_t monotonicUs,
            const std::vector<IngressEvent>& incomingEvents = {});

        std::size_t _capacity{ 256 };
        std::uint64_t _nextSeq{ 1 };
        std::uint64_t _lastAllocatedOrderedSeq{ 0 };
        std::uint64_t _lastConsumedOrderedSeq{ 0 };
        std::uint64_t _lastLegacySequence{ 0 };
        std::uint64_t _latestPadGeneration{ 0 };
        std::uint64_t _latestSourceGeneration{ 0 };
        std::uint64_t _latestGamepadConnectionGeneration{ 0 };
        std::uint64_t _latestKbmGameplayGeneration{ 0 };
        std::uint64_t _captureGeneration{ 0 };
        std::uint64_t _capturedPadGeneration{ 0 };
        std::uint64_t _capturedSourceGeneration{ 0 };
        std::uint64_t _capturedGamepadConnectionGeneration{ 0 };
        std::uint64_t _capturedKbmGameplayGeneration{ 0 };
        std::uint64_t _inputStateEpoch{ 1 };
        std::uint64_t _gamepadSessionId{ 0 };
        std::uint64_t _controlMapFingerprint{ 0 };
        IngressBoundaryKey _boundaryKey{};
        GamepadConnectivity _gamepadConnectivity{ GamepadConnectivity::Disconnected };
        std::size_t _pendingLegacySnapshots{ 0 };
        std::uint32_t _previousDigitalMask{ 0 };
        bool _edgeHistoryLost{ false };
        std::array<std::uint64_t, 32> _digitalDownAtUs{};
        std::optional<UiSnapshotPayload> _lastUiSnapshot;
        std::optional<LatestPadState> _latestPadState;
        std::optional<LatestSourceEvidence> _latestSourceEvidence;
        std::optional<GamepadConnectionFacts> _latestGamepadConnection;
        std::optional<LatestKbmGameplayFacts> _latestKbmGameplay;
        std::deque<IngressEvent> _queue;
        mutable std::mutex _mutex;
    };
}
