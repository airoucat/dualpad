#pragma once

#include "input_v2/actions/LegacyInteractionInputAdapter.h"
#include "input_v2/ingress/IngressBoundaryKey.h"
#include "input_v2/ingress/IngressMarkers.h"
#include "input_v2/ingress/IngressRecovery.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace dualpad::input_v2::ingress
{
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

    struct FactFrame
    {
        std::uint32_t manifestEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint32_t deviceFamilyRevision{ 0 };
        std::uint64_t monotonicUs{ 0 };
        std::vector<actions::ControlSample> controlSamples;
        std::vector<actions::ControlSample> pulseLedger;
        presentation::SourceEvidenceSnapshot sourceEvidence;
        std::optional<dualpad::input::PadEventSnapshot> legacySnapshot;
        FactHealth health;
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

        void ApplyEventToWindow(const IngressEvent& event);
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
        void HandleSourceEvidence(std::vector<AssembledFactFrame>& frames, const IngressEvent& event);
    };

    bool ShouldDispatchToInteractionEngine(const AssembledFactFrame& frame);
    actions::KernelFrame BuildKernelFrame(const AssembledFactFrame& frame);
}
