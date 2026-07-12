#pragma once

#include "input_v2/actions/LegacyInteractionInputAdapter.h"
#include "input_v2/ingress/IngressBoundaryKey.h"
#include "input_v2/ingress/LatestPadState.h"
#include "input_v2/ingress/IngressMarkers.h"
#include "input_v2/ingress/IngressRecovery.h"
#include "input_v2/ingress/GamepadActivityClassifier.h"
#include "input_v2/ingress/KbmGameplayFacts.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace dualpad::input_v2::ingress
{
    struct IngressCapture;

    struct InputFactCoherenceKey
    {
        std::uint64_t captureGeneration{ 0 };
        std::uint64_t orderedCutoffSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint64_t manifestEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    enum class AssembledFrameKind : std::uint8_t
    {
        Stable = 0,
        Transition
    };

    struct FactHealth
    {
        bool pendingBoundaryMarkerPair{ false };
        bool boundaryMarkerMismatch{ false };
        bool queueOverflow{ false };
        bool sequenceGap{ false };
        bool coalescedSnapshot{ false };
        bool crossContextMismatch{ false };
    };

    struct OverflowCompactionDebugSummary
    {
        bool transitionObserved{ false };
        bool typedCompactionApplied{ false };
        bool retainedManifest{ false };
        bool retainedUi{ false };
        bool retainedDeviceFamily{ false };
        bool retainedSourceEvidence{ false };
        bool droppedControlSamples{ false };
        bool droppedPulseLedger{ false };
        bool droppedLegacySnapshot{ false };
        std::string debugSummary;
    };

    struct FactFrame
    {
        std::uint32_t manifestEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint32_t deviceFamilyRevision{ 0 };
        std::uint64_t monotonicUs{ 0 };
        std::uint64_t latestPadStateGeneration{ 0 };
        std::uint64_t latestSourceEvidenceGeneration{ 0 };
        std::vector<actions::ControlSample> controlSamples;
        std::vector<actions::ControlSample> pulseLedger;
        presentation::SourceEvidenceSnapshot sourceEvidence;
        std::optional<dualpad::input::PadEventSnapshot> legacySnapshot;
        InputFactCoherenceKey coherence{};
        std::optional<GamepadConnectionFacts> gamepadConnection;
        std::optional<LatestKbmGameplayFacts> kbmGameplay;
        FactHealth health;
        std::optional<OverflowCompactionDebugSummary> overflowCompaction;
    };

    struct TransitionFrameMeta
    {
        IngressBoundaryKey from{};
        IngressBoundaryKey to{};
        TransitionReason reason{ TransitionReason::BoundaryKeyChanged };
        bool requestSoftResync{ false };
        bool requestHardResync{ false };
        bool flushPendingPulseEdges{ false };
    };

    struct AssembledFactFrame
    {
        AssembledFrameKind kind{ AssembledFrameKind::Stable };
        std::uint64_t firstSeq{ 0 };
        std::uint64_t lastSeq{ 0 };
        IngressBoundaryKey boundaryKey{};
        FactFrame facts{};
        TransitionFrameMeta transition{};
    };

    class FrameAssembler
    {
    public:
        std::vector<AssembledFactFrame> Assemble(const std::vector<IngressEvent>& events);
        std::vector<AssembledFactFrame> Assemble(const IngressCapture& capture);
        std::vector<AssembledFactFrame> Assemble(
            const std::vector<IngressEvent>& events,
            const std::optional<LatestPadState>& latestPadState,
            const std::optional<LatestSourceEvidence>& latestSourceEvidence);
        void Reset();

    private:
        struct Window
        {
            bool open{ false };
            std::uint64_t firstSeq{ 0 };
            std::uint64_t lastSeq{ 0 };
            std::uint64_t firstMonotonicUs{ 0 };
            std::uint64_t lastMonotonicUs{ 0 };
            IngressBoundaryKey key{};
            FactFrame facts{};
        };

        std::optional<DeviceFamilyChangedPayload> _pendingDeviceMarker;
        IngressBoundaryKey _currentKey{};
        FactFrame _latestFacts{};
        Window _window{};
        std::uint64_t _lastConsumedSeq{ 0 };
        std::uint64_t _lastMonotonicUs{ 0 };
        std::uint64_t _lastLatestPadGeneration{ 0 };
        std::uint64_t _lastLatestSourceGeneration{ 0 };
        std::array<std::uint64_t, 32> _gamepadDownAtUs{};

        void ApplyEventToWindow(const IngressEvent& event);
        void ApplyOverflowCompaction(std::vector<AssembledFactFrame>& frames, const IngressEvent& event);
        void ApplyFactsFromBoundaryKey(FactFrame& facts, const IngressBoundaryKey& key) const;
        void FlushWindow(std::vector<AssembledFactFrame>& frames);
        void EmitTransition(
            std::vector<AssembledFactFrame>& frames,
            const IngressBoundaryKey& from,
            const IngressBoundaryKey& to,
            TransitionReason reason,
            FactHealth health = {});
        void StartWindow(const IngressEvent& event);
        void HandleBoundaryChange(std::vector<AssembledFactFrame>& frames, const IngressEvent& event, IngressBoundaryKey nextKey, TransitionReason reason);
        bool HandleOrderingViolation(std::vector<AssembledFactFrame>& frames, const IngressEvent& event);
        void HandleSourceEvidence(std::vector<AssembledFactFrame>& frames, const IngressEvent& event);
        void ApplyLatestPadState(const LatestPadState& latest);
        void ApplyLatestSourceEvidence(
            std::vector<AssembledFactFrame>& frames,
            const LatestSourceEvidence& latest);
    };

    bool ShouldDispatchToInteractionEngine(const AssembledFactFrame& frame);
    actions::KernelFrame BuildKernelFrame(const AssembledFactFrame& frame);
}
