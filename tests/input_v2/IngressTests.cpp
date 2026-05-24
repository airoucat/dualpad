#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/ingress/LegacyIngressAdapter.h"
#include "input_v2/ingress/IngressRecovery.h"
#include "input_v2/config/ActionManifestPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
    using namespace dualpad::input_v2;
    namespace input = dualpad::input;

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

    void TestLegacySnapshotAdapterProducesControlSamplesAndPulseLedger()
    {
        input::PadEventSnapshot snapshot{};
        snapshot.sequence = 10;
        snapshot.firstSequence = 10;
        snapshot.sourceTimestampUs = 1234;
        snapshot.contextEpoch = 77;
        snapshot.state.timestampUs = 1234;
        snapshot.state.sequence = 10;
        snapshot.state.buttons.digitalMask = 0x3;
        snapshot.state.leftStick.x = 0.25f;
        snapshot.state.leftStick.y = -0.5f;
        snapshot.state.rightTrigger.normalized = 0.75f;

        input::PadEvent press{};
        press.type = input::PadEventType::ButtonPress;
        press.code = 0x1;
        press.timestampUs = 1200;
        snapshot.events.Push(press);

        input::PadEvent release{};
        release.type = input::PadEventType::ButtonRelease;
        release.code = 0x2;
        release.timestampUs = 1210;
        snapshot.events.Push(release);

        input::PadEvent axis{};
        axis.type = input::PadEventType::AxisChange;
        axis.axis = input::PadAxisId::RightTrigger;
        axis.value = 0.75f;
        axis.timestampUs = 1220;
        snapshot.events.Push(axis);

        const auto converted = ingress::ConvertLegacySnapshotToIngressEvents(snapshot, 9);
        Require(converted.size() == 2, "snapshot with continuous sequence must produce ui + pad ingress events");
        Require(converted[1].kind == ingress::IngressKind::PadSnapshot, "legacy snapshot must become PadSnapshot");
        Require(converted[1].pad.samples.size() >= 6, "digital down and analog values must become control samples");

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(AssignSeq({
            Manifest(1),
            DeviceMarker(presentation::DeviceFamily::Gamepad, 1),
            SourceEvidence(1),
            converted[0],
            converted[1]
        }));
        const auto& stable = LastFrame(frames);
        Require(stable.kind == ingress::AssembledFrameKind::Stable, "converted snapshot must reach stable frame");
        Require(stable.facts.pulseLedger.size() == 2, "legacy press/release edges must enter pulse ledger");
        Require(stable.facts.controlSamples.size() >= 6, "stable facts must retain non-empty control samples");
    }

    void TestLegacySequenceDiscontinuityProducesSequenceGap()
    {
        input::PadEventSnapshot snapshot{};
        snapshot.sequence = 12;
        snapshot.firstSequence = 12;
        const auto converted = ingress::ConvertLegacySnapshotToIngressEvents(snapshot, 10);
        Require(!converted.empty(), "converted events must not be empty");
        Require(converted[0].kind == ingress::IngressKind::SequenceGap, "last observed 10 then first 12 must produce SequenceGap");
    }

    void TestManifestPublisherProducesIngressMarker()
    {
        ingress::IngressHub::GetSingleton().ResetForTests();
        actions::CompiledActionManifest manifest{};
        manifest.manifestEpoch = 42;
        manifest.actions = {
            actions::ActionDefinition{ .id = "Jump", .valueKind = actions::ActionValueKind::Digital }
        };
        manifest.bindings.push_back(actions::CompiledBinding{
            .actionId = "Jump",
            .baseSetId = "GameplayBase",
            .legacyTrigger = input::Trigger{ .type = input::TriggerType::Button, .code = 10 }
        });

        config::CompiledConfigBundle bundle{};
        bundle.manifestEpoch = 42;
        bundle.catalog.manifestEpoch = 42;
        bundle.manifest = manifest;
        bundle.manifest.legacyBindingProjection.manifestEpoch = 42;

        Require(
            config::ActionManifestPublisher::GetSingleton().PublishPromotedBundle(bundle, 42),
            "manifest publish must succeed");
        const auto drained = ingress::IngressHub::GetSingleton().Drain();
        Require(!drained.empty(), "manifest publish seam must enqueue ingress marker");
        Require(drained.back().kind == ingress::IngressKind::ManifestEpochChanged, "manifest publish marker kind required");
        Require(drained.back().manifest.manifestEpoch == 42, "manifest marker payload is authoritative epoch");
    }

    void TestDeviceFamilyProducerProducesMarkerAndPairedSourceEvidence()
    {
        ingress::IngressHub::GetSingleton().ResetForTests();
        presentation::DeviceFamilyIngressPublisher publisher;
        presentation::SourceEvidenceCollector collector;
        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 9;
        contextSnapshot.menuStackRevision = 10;

        const auto publication = publisher.Publish(
            presentation::DeviceFamily::Gamepad,
            presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            100);
        const auto frame = collector.CollectAfterDeviceFamilyIngress(publication, contextSnapshot, 100);
        ingress::PublishSourceEvidenceFrameToIngressHub(frame);

        const auto drained = ingress::IngressHub::GetSingleton().Drain();
        Require(drained.size() == 2, "device family seam must enqueue marker plus source evidence");
        Require(drained[0].kind == ingress::IngressKind::DeviceFamilyChanged, "device marker must be first");
        Require(drained[1].kind == ingress::IngressKind::SourceEvidence, "source evidence must pair after marker");
        Require(
            drained[0].deviceFamily.deviceFamilyRevision ==
                drained[1].sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision,
            "source evidence revision must only pair/mirror marker payload");
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
    TestLegacySnapshotAdapterProducesControlSamplesAndPulseLedger();
    TestLegacySequenceDiscontinuityProducesSequenceGap();
    TestManifestPublisherProducesIngressMarker();
    TestDeviceFamilyProducerProducesMarkerAndPairedSourceEvidence();
    TestStableMergeKeepsPulseLedger();
    TestBoundaryChangeFlushesStableThenTransition();
    TestRecoveryMarkersMapFailClosed();
    TestDeviceMarkerMismatchFailsClosed();
    TestBuildKernelFrameDoesNotAcceptTransition();
    std::cout << "DualPadIngressTests passed\n";
    return 0;
}
