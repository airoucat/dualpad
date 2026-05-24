#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/ingress/IngressRecovery.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
    using namespace dualpad::input_v2;

    void Require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    ingress::IngressEvent Ui(std::uint32_t contextRevision, std::uint32_t menuStackRevision)
    {
        ingress::IngressEvent event{};
        event.kind = ingress::IngressKind::UiSnapshot;
        event.ui = ingress::UiSnapshotPayload{
            .contextRevision = contextRevision,
            .menuStackRevision = menuStackRevision
        };
        return event;
    }

    ingress::IngressEvent Manifest(std::uint32_t epoch)
    {
        ingress::IngressEvent event{};
        event.kind = ingress::IngressKind::ManifestEpochChanged;
        event.manifest = ingress::ManifestEpochChangedPayload{ .manifestEpoch = epoch };
        return event;
    }

    ingress::IngressEvent DeviceMarker(
        presentation::DeviceFamily family,
        std::uint32_t revision)
    {
        ingress::IngressEvent event{};
        event.kind = ingress::IngressKind::DeviceFamilyChanged;
        event.deviceFamily = ingress::DeviceFamilyChangedPayload{
            .family = family,
            .deviceFamilyRevision = revision
        };
        return event;
    }

    ingress::IngressEvent SourceEvidence(std::uint32_t deviceFamilyRevision)
    {
        ingress::IngressEvent event{};
        event.kind = ingress::IngressKind::SourceEvidence;
        event.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision = deviceFamilyRevision;
        return event;
    }

    ingress::IngressEvent PadSample(std::uint32_t code, bool down, bool pressed, bool released)
    {
        ingress::IngressEvent event{};
        event.kind = ingress::IngressKind::PadSnapshot;
        event.pad.samples.push_back(actions::ControlSample{
            .path = actions::ControlPath{
                .kind = actions::ControlPathKind::DigitalButton,
                .code = code
            },
            .down = down,
            .pressed = pressed,
            .released = released
        });
        return event;
    }

    std::vector<ingress::IngressEvent> AssignSeq(std::vector<ingress::IngressEvent> events)
    {
        std::uint64_t seq = 1;
        for (auto& event : events) {
            event.seq = seq++;
            event.monotonicUs = event.seq * 100;
        }
        return events;
    }

    const ingress::AssembledFactFrame* FindTransition(
        const std::vector<ingress::AssembledFactFrame>& frames,
        ingress::TransitionReason reason)
    {
        for (const auto& frame : frames) {
            if (frame.kind == ingress::AssembledFrameKind::Transition &&
                frame.transition.reason == reason) {
                return &frame;
            }
        }
        return nullptr;
    }

    const ingress::AssembledFactFrame& LastFrame(const std::vector<ingress::AssembledFactFrame>& frames)
    {
        Require(!frames.empty(), "frames must not be empty");
        return frames.back();
    }

    void TestHubAssignsSeqAndEmitsOverflowMarker()
    {
        ingress::IngressHub hub{ 2 };
        Require(hub.PushEvent(PadSample(1, true, true, false)), "first event must enqueue");
        Require(hub.PushEvent(PadSample(2, true, true, false)), "second event must enqueue");
        Require(!hub.PushEvent(PadSample(3, true, true, false)), "third event must report overflow");

        const auto drained = hub.Drain();
        Require(drained.size() == 1, "overflow must replace backlog with one marker");
        Require(drained[0].kind == ingress::IngressKind::QueueOverflow, "overflow marker must be formal event");
        Require(drained[0].seq == 3, "hub must assign monotonic seq to overflow marker");
    }

    void TestStableMergeKeepsPulseLedger()
    {
        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(AssignSeq({
            Manifest(7),
            Ui(11, 12),
            DeviceMarker(presentation::DeviceFamily::Gamepad, 3),
            SourceEvidence(3),
            PadSample(42, true, true, false),
            PadSample(42, false, false, true)
        }));

        Require(frames.size() >= 3, "manifest and device marker boundaries must produce transitions before stable frame");
        Require(frames[0].kind == ingress::AssembledFrameKind::Transition, "first frame must be transition");
        Require(frames[0].transition.reason == ingress::TransitionReason::ManifestEpochChanged, "manifest transition reason required");
        Require(frames[0].transition.requestHardResync, "manifest transition must hard reset");
        Require(FindTransition(frames, ingress::TransitionReason::BoundaryKeyChanged) != nullptr, "device marker transition uses boundary reason");
        const auto& stable = LastFrame(frames);
        Require(stable.kind == ingress::AssembledFrameKind::Stable, "last frame must be stable");
        Require(stable.boundaryKey == (ingress::IngressBoundaryKey{ 7, 11, 12, 3 }), "boundary key must use four marker/context fields");
        Require(stable.facts.pulseLedger.size() == 2, "press/release pulse ledger must not be collapsed");
        Require(stable.facts.controlSamples.size() == 1, "latest steady sample must be retained once");
        Require(!stable.facts.health.boundaryMarkerMismatch, "valid marker pairing must stay healthy");
    }

    void TestBoundaryChangeFlushesStableThenTransition()
    {
        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(AssignSeq({
            Manifest(1),
            Ui(10, 20),
            DeviceMarker(presentation::DeviceFamily::Gamepad, 1),
            SourceEvidence(1),
            PadSample(7, true, true, false),
            Ui(11, 20),
            PadSample(7, false, false, true)
        }));

        const auto* transition = FindTransition(frames, ingress::TransitionReason::BoundaryKeyChanged);
        Require(transition != nullptr, "context change uses boundary transition");
        Require(!transition->transition.requestHardResync, "plain context boundary must not hard reset");
        const auto& stable = LastFrame(frames);
        Require(stable.kind == ingress::AssembledFrameKind::Stable, "new boundary stable starts after transition");
        Require(stable.firstSeq == 6, "new stable frame must start at the boundary-changing event");
    }

    void TestRecoveryMarkersMapFailClosed()
    {
        ingress::FrameAssembler assembler;
        auto frames = assembler.Assemble(AssignSeq({
            Manifest(1),
            Ui(1, 1),
            DeviceMarker(presentation::DeviceFamily::Gamepad, 1),
            SourceEvidence(1),
            ingress::MakeSequenceGapEvent(),
            ingress::MakeQueueOverflowEvent()
        }));

        const auto* gap = FindTransition(frames, ingress::TransitionReason::SequenceGap);
        const auto* overflow = FindTransition(frames, ingress::TransitionReason::QueueOverflow);
        Require(gap != nullptr, "sequence gap transition required");
        Require(gap->transition.requestSoftResync, "sequence gap must soft resync");
        Require(ToGameplayRecoveryInput(*gap).softResyncRequested, "sequence gap maps to gameplay recovery input");
        Require(overflow != nullptr, "queue overflow transition required");
        Require(overflow->transition.requestHardResync, "queue overflow must hard reset");
        Require(ToGameplayRecoveryInput(*overflow).hardResetRequested, "queue overflow maps to hard recovery input");
    }

    void TestDeviceMarkerMismatchFailsClosed()
    {
        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(AssignSeq({
            Manifest(1),
            Ui(1, 1),
            DeviceMarker(presentation::DeviceFamily::Gamepad, 7),
            SourceEvidence(8)
        }));

        Require(frames.size() >= 2, "marker mismatch must stop before publishing stable frame");
        Require(frames[0].transition.reason == ingress::TransitionReason::ManifestEpochChanged, "manifest transition still appears");
        const auto* reset = FindTransition(frames, ingress::TransitionReason::ExplicitReset);
        Require(reset != nullptr, "mismatch must fail closed through explicit reset");
        Require(reset->transition.requestHardResync, "marker mismatch must hard reset");
        Require(reset->facts.health.boundaryMarkerMismatch, "mismatch health marker required");
    }

    void TestBuildKernelFrameDoesNotAcceptTransition()
    {
        ingress::AssembledFactFrame transition{};
        transition.kind = ingress::AssembledFrameKind::Transition;
        transition.transition.reason = ingress::TransitionReason::QueueOverflow;
        Require(!ingress::ShouldDispatchToInteractionEngine(transition), "transition frame must not enter interaction engine");

        ingress::AssembledFactFrame stable{};
        stable.kind = ingress::AssembledFrameKind::Stable;
        stable.boundaryKey = ingress::IngressBoundaryKey{ 2, 3, 4, 5 };
        const auto kernel = ingress::BuildKernelFrame(stable);
        Require(kernel.facts.manifestEpoch == 2, "kernel manifest epoch mirrors boundary key");
        Require(kernel.facts.contextRevision == 3, "kernel context revision mirrors boundary key");
        Require(kernel.facts.menuStackRevision == 4, "kernel menu stack revision mirrors boundary key");
        Require(kernel.facts.deviceFamilyRevision == 5, "kernel device family revision mirrors boundary key");
    }
}

int main()
{
    TestHubAssignsSeqAndEmitsOverflowMarker();
    TestStableMergeKeepsPulseLedger();
    TestBoundaryChangeFlushesStableThenTransition();
    TestRecoveryMarkersMapFailClosed();
    TestDeviceMarkerMismatchFailsClosed();
    TestBuildKernelFrameDoesNotAcceptTransition();
    std::cout << "DualPadIngressTests passed\n";
    return 0;
}
