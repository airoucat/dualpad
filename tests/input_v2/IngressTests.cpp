#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/ingress/LegacyIngressAdapter.h"
#include "input_v2/ingress/LiveInputFactProducer.h"
#include "input_v2/ingress/IngressRecovery.h"
#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/actions/InteractionEngine.h"
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

    input::PadEventSnapshot LiveHidSnapshot(
        std::uint64_t sequence,
        std::uint32_t mask,
        std::uint64_t timestampUs)
    {
        input::PadEventSnapshot snapshot{};
        snapshot.sequence = sequence;
        snapshot.firstSequence = sequence;
        snapshot.sourceTimestampUs = timestampUs;
        snapshot.contextEpoch = 7;
        snapshot.state.sequence = sequence;
        snapshot.state.timestampUs = timestampUs;
        snapshot.state.buttons.digitalMask = mask;
        return snapshot;
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

    const ingress::AssembledFactFrame& LastStableFrame(const std::vector<ingress::AssembledFactFrame>& frames)
    {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
            if (it->kind == ingress::AssembledFrameKind::Stable) {
                return *it;
            }
        }
        Require(false, "frames must contain a stable frame");
        return frames.back();
    }

    const actions::ControlSample* FindPulse(
        const ingress::FactFrame& facts,
        std::uint32_t code,
        bool pressed,
        bool released)
    {
        for (const auto& sample : facts.pulseLedger) {
            if (sample.path.kind == actions::ControlPathKind::DigitalButton &&
                sample.path.code == code &&
                sample.pressed == pressed &&
                sample.released == released) {
                return &sample;
            }
        }
        return nullptr;
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

    void TestHubOverflowCompactsBoundaryFactsAndDropsVolatileInput()
    {
        ingress::IngressHub hub{ 4 };
        Require(hub.PushEvent(Manifest(9)), "manifest marker must enqueue");
        Require(hub.PushEvent(Ui(21, 22)), "ui snapshot must enqueue");
        Require(
            hub.PushEvent(DeviceMarker(presentation::DeviceFamily::Gamepad, 7)),
            "device marker must enqueue");
        Require(hub.PushEvent(PadSample(99, true, true, false)), "volatile pad sample must enqueue");
        Require(!hub.PushEvent(SourceEvidence(7)), "source evidence should trigger overflow");

        const auto drained = hub.Drain();
        Require(drained.size() == 1, "overflow compaction emits one marker");
        Require(drained[0].kind == ingress::IngressKind::QueueOverflow, "overflow marker kind required");
        Require(drained[0].overflow.hasManifest, "overflow compaction must retain latest manifest");
        Require(drained[0].overflow.hasUi, "overflow compaction must retain latest ui snapshot");
        Require(drained[0].overflow.hasDeviceFamily, "overflow compaction must retain latest device marker");
        Require(drained[0].overflow.hasSourceEvidence, "overflow compaction must retain latest source evidence");
        Require(drained[0].overflow.manifest.manifestEpoch == 9, "manifest epoch must be retained");
        Require(drained[0].overflow.ui.contextRevision == 21, "ui context revision must be retained");
        Require(drained[0].overflow.ui.menuStackRevision == 22, "ui menu stack revision must be retained");
        Require(drained[0].overflow.deviceFamily.deviceFamilyRevision == 7, "device revision must be retained");
        Require(
            drained[0].overflow.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision == 7,
            "source evidence revision must be retained");
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

    void TestLegacySnapshotBatchOverflowRejectsPartialEvents()
    {
        ingress::IngressHub hub{ 1 };

        input::PadEventSnapshot snapshot{};
        snapshot.sequence = 1;
        snapshot.firstSequence = 1;
        snapshot.sourceTimestampUs = 50'000;
        snapshot.contextEpoch = 7;
        snapshot.state.timestampUs = 50'000;
        snapshot.state.sequence = 1;

        Require(!hub.PushPadSnapshot(snapshot), "multi-event legacy snapshot must fail atomically when capacity is too small");
        Require(hub.PendingLegacySnapshotCount() == 0, "rejected legacy snapshot batch must not increment pending snapshot count");

        const auto drained = hub.Drain();
        Require(drained.size() == 1, "rejected legacy snapshot batch must leave only one recovery marker");
        Require(drained[0].kind == ingress::IngressKind::QueueOverflow, "rejected legacy snapshot batch must publish QueueOverflow");
    }

    void TestRejectedLegacySnapshotAdvancesWatermarkAsDroppedRange()
    {
        ingress::IngressHub hub{ 2 };

        Require(hub.PushEvent(PadSample(99, true, true, false)), "setup event must occupy one queue slot");
        Require(
            !hub.PushPadSnapshot(LiveHidSnapshot(1, 0x0, 1'000)),
            "first legacy snapshot batch must overflow when only one slot is available");
        const auto recovery = hub.Drain();
        Require(recovery.size() == 1, "overflowed batch must drain to one recovery marker");
        Require(recovery[0].kind == ingress::IngressKind::QueueOverflow, "overflowed batch must drain QueueOverflow");

        Require(
            hub.PushPadSnapshot(LiveHidSnapshot(2, 0x0, 2'000)),
            "next contiguous snapshot must be accepted after overflow drain");
        const auto accepted = hub.Drain();
        Require(accepted.size() == 2, "accepted contiguous snapshot must publish ui + pad events");
        Require(
            accepted[0].kind != ingress::IngressKind::SequenceGap,
            "rejected snapshot advances the dropped-range watermark, so the next contiguous snapshot must not emit SequenceGap");
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

    void TestLiveHidMaskEdgesProducePulseLedger()
    {
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& hub = ingress::IngressHub::GetSingleton();
        (void)hub.PushEvent(Manifest(42));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(1, 0x0, 1'000));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(2, 0x1, 2'000));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(3, 0x0, 3'000));

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(hub.Drain());
        const auto& stable = LastFrame(frames);
        const auto* press = FindPulse(stable.facts, 0x1, true, false);
        const auto* release = FindPulse(stable.facts, 0x1, false, true);
        Require(press != nullptr, "HID mask 0 -> 1 must produce a press pulse");
        Require(release != nullptr, "HID mask 1 -> 0 must produce a release pulse");
        Require(press->down, "press sample must be down");
        Require(press->downAtUs == 2'000, "press sample must carry press downAtUs");
        Require(!release->down, "release sample must not be down");
        Require(release->downAtUs == 2'000, "release sample must retain the original press downAtUs");
    }

    void TestLiveHidPressSampleTriggersInteractionEngine()
    {
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& hub = ingress::IngressHub::GetSingleton();
        (void)hub.PushEvent(Manifest(42));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(10, 0x0, 10'000));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(11, 0x1, 11'000));

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(hub.Drain());
        const auto& stable = LastFrame(frames);
        const auto kernel = ingress::BuildKernelFrame(stable);

        actions::CompiledActionManifest manifest{};
        manifest.manifestEpoch = 42;
        manifest.actions = {
            actions::ActionDefinition{ .id = "Jump", .valueKind = actions::ActionValueKind::Digital }
        };
        manifest.bindings.push_back(actions::CompiledBinding{
            .actionId = "Jump",
            .baseSetId = "GameplayBase",
            .legacyTrigger = input::Trigger{ .type = input::TriggerType::Button, .code = 0x1 }
        });

        const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
        Require(compiled.ok, compiled.message.c_str());

        actions::ActionSetStack stack{};
        stack.baseSetId = "GameplayBase";
        actions::InteractionEngine engine;
        actions::InteractionStateStore state;
        const auto resolved = engine.Resolve(compiled.graph, stack, kernel, state);
        Require(resolved.changes.size() == 1, "live HID press sample must trigger an action phase");
        Require(resolved.changes[0].actionId == "Jump", "live HID press must resolve the bound action");
        Require(resolved.changes[0].phase == actions::ActionPhase::Press, "live HID press must emit Press");
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

    void TestLiveGamepadInputPublishesSourceEvidence()
    {
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 21;
        contextSnapshot.menuStackRevision = 22;
        producer.PublishGamepadSourceEvidence(contextSnapshot, 44'000);

        const auto drained = ingress::IngressHub::GetSingleton().Drain();
        Require(drained.size() == 2, "live gamepad evidence must enqueue marker plus source evidence");
        Require(drained[0].kind == ingress::IngressKind::DeviceFamilyChanged, "live gamepad evidence marker must be first");
        Require(drained[1].kind == ingress::IngressKind::SourceEvidence, "live gamepad source evidence must pair after marker");
        Require(
            drained[0].deviceFamily.deviceFamilyRevision ==
                drained[1].sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision,
            "live SourceEvidence must mirror the marker deviceFamilyRevision");
        Require(
            drained[1].sourceEvidence.deviceFamilyEvidence.family == presentation::DeviceFamily::Gamepad,
            "live SourceEvidence must publish gamepad family");
        Require(drained[1].sourceEvidence.gamepadEvidence, "live SourceEvidence must record gamepad evidence");
    }

    void TestLiveKeyboardMouseEvidencePublishesTakeoverAndReclaim()
    {
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 31;
        contextSnapshot.menuStackRevision = 32;

        producer.PublishGamepadSourceEvidence(contextSnapshot, 100'000);
        auto drained = ingress::IngressHub::GetSingleton().Drain();
        Require(drained.size() == 2, "gamepad evidence must publish marker plus SourceEvidence");
        Require(drained[1].sourceEvidence.gamepadEvidence, "gamepad evidence must set gamepadEvidence");
        Require(drained[1].sourceEvidence.gamepadLease, "gamepad evidence must establish gamepad lease");

        producer.PublishKeyboardSourceEvidence(contextSnapshot, 0x1E, 101'000);
        drained = ingress::IngressHub::GetSingleton().Drain();
        Require(drained.size() == 2, "keyboard evidence must publish takeover marker plus SourceEvidence");
        Require(
            drained[0].deviceFamily.family == presentation::DeviceFamily::KeyboardMouse,
            "keyboard evidence must publish KeyboardMouse marker");
        Require(drained[1].sourceEvidence.keyboardEvidence, "keyboard evidence must set keyboardEvidence");
        Require(!drained[1].sourceEvidence.gamepadEvidence, "keyboard evidence must clear gamepad evidence");
        Require(!drained[1].sourceEvidence.gamepadLease, "keyboard evidence must clear gamepad lease");

        producer.PublishGamepadSourceEvidence(contextSnapshot, 102'000);
        drained = ingress::IngressHub::GetSingleton().Drain();
        Require(drained.size() == 2, "gamepad reclaim must publish marker plus SourceEvidence");
        Require(
            drained[0].deviceFamily.family == presentation::DeviceFamily::Gamepad,
            "gamepad reclaim must publish Gamepad marker");
        Require(drained[1].sourceEvidence.gamepadEvidence, "gamepad reclaim must restore gamepadEvidence");
        Require(!drained[1].sourceEvidence.keyboardEvidence, "gamepad reclaim must clear keyboardEvidence");

        producer.PublishMouseMoveSourceEvidence(contextSnapshot, 5, -3, 103'000);
        drained = ingress::IngressHub::GetSingleton().Drain();
        Require(drained.size() == 2, "mouse move evidence must publish takeover marker plus SourceEvidence");
        Require(drained[1].sourceEvidence.mouseMoveEvidence, "mouse move evidence must set mouseMoveEvidence");
        Require(
            drained[1].sourceEvidence.pointerSignal == presentation::PointerSignal::HoverOnly,
            "mouse move evidence must publish hover pointer signal");

        producer.PublishGamepadSourceEvidence(contextSnapshot, 104'000);
        (void)ingress::IngressHub::GetSingleton().Drain();
        producer.PublishMouseButtonSourceEvidence(contextSnapshot, 105'000);
        drained = ingress::IngressHub::GetSingleton().Drain();
        Require(drained.size() == 2, "mouse button evidence must publish takeover marker plus SourceEvidence");
        Require(drained[1].sourceEvidence.mouseButtonEvidence, "mouse button evidence must set mouseButtonEvidence");
        Require(
            drained[1].sourceEvidence.pointerSignal == presentation::PointerSignal::PointerActive,
            "mouse button evidence must publish active pointer signal");
    }

    void TestSyntheticKeyboardWindowDoesNotPublishKeyboardMouseTakeover()
    {
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 41;
        contextSnapshot.menuStackRevision = 42;

        producer.PublishGamepadSourceEvidence(contextSnapshot, 200'000);
        (void)ingress::IngressHub::GetSingleton().Drain();

        producer.MarkSyntheticKeyboardScancode(0x64, 1, 250'000, 201'000);
        producer.PublishKeyboardSourceEvidence(contextSnapshot, 0x64, 201'100);

        const auto drained = ingress::IngressHub::GetSingleton().Drain();
        Require(drained.size() == 1, "synthetic keyboard evidence must not publish a device-family takeover marker");
        Require(drained[0].kind == ingress::IngressKind::SourceEvidence, "synthetic keyboard evidence should only mirror SourceEvidence");
        Require(
            drained[0].sourceEvidence.deviceFamilyEvidence.family == presentation::DeviceFamily::Gamepad,
            "synthetic keyboard evidence must keep the current Gamepad family");
        Require(!drained[0].sourceEvidence.keyboardEvidence, "synthetic keyboard evidence must not set keyboardEvidence");
        Require(drained[0].sourceEvidence.syntheticKeyboardWindow, "synthetic keyboard evidence must mark the synthetic window");
        Require(drained[0].sourceEvidence.gamepadLease, "synthetic keyboard evidence must not clear the gamepad lease");
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

    void TestFrameAssemblerDoesNotSortOutOfOrderEvents()
    {
        auto events = AssignSeq({
            Manifest(1),
            PadSample(1, true, true, false),
            PadSample(1, false, false, true)
        });
        std::swap(events[1], events[2]);

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(events);
        Require(
            FindTransition(frames, ingress::TransitionReason::SequenceGap) != nullptr,
            "out-of-order seq must fail closed instead of being sorted back into order");
    }

    void TestFrameAssemblerRejectsMonotonicTimeRegression()
    {
        auto events = AssignSeq({
            Manifest(1),
            PadSample(1, true, true, false),
            PadSample(1, false, false, true)
        });
        events[2].monotonicUs = events[1].monotonicUs - 1;

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(events);
        Require(
            FindTransition(frames, ingress::TransitionReason::SequenceGap) != nullptr,
            "monotonic time regression must fail closed through a transition");

        const auto& stable = LastStableFrame(frames);
        Require(
            FindPulse(stable.facts, 1, false, true) == nullptr,
            "time-regressed volatile release must not enter stable pulse ledger");
    }

    void TestFrameAssemblerOverflowPayloadBuildsBoundaryBaseline()
    {
        ingress::IngressHub hub{ 4 };
        Require(hub.PushEvent(Manifest(9)), "manifest marker must enqueue");
        Require(hub.PushEvent(Ui(21, 22)), "ui snapshot must enqueue");
        Require(
            hub.PushEvent(DeviceMarker(presentation::DeviceFamily::Gamepad, 7)),
            "device marker must enqueue");
        Require(hub.PushEvent(PadSample(99, true, true, false)), "volatile pad sample must enqueue");
        Require(!hub.PushEvent(SourceEvidence(7)), "source evidence should trigger overflow");

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(hub.Drain());
        Require(frames.size() == 2, "overflow payload must produce transition plus baseline stable");
        Require(frames[0].kind == ingress::AssembledFrameKind::Transition, "overflow transition must come first");
        Require(frames[0].transition.reason == ingress::TransitionReason::QueueOverflow, "transition reason must be overflow");
        const auto& stable = frames[1];
        Require(stable.kind == ingress::AssembledFrameKind::Stable, "overflow payload must rebuild stable baseline");
        Require(
            stable.boundaryKey == (ingress::IngressBoundaryKey{ 9, 21, 22, 7 }),
            "overflow baseline must retain latest boundary facts");
        Require(stable.facts.controlSamples.empty(), "overflow baseline must drop volatile control samples");
        Require(stable.facts.pulseLedger.empty(), "overflow baseline must drop volatile pulse ledger");
        Require(!stable.facts.legacySnapshot, "overflow baseline must drop legacy snapshot payload");
        Require(stable.facts.health.queueOverflow, "overflow baseline must remain degraded for the current frame");
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

    void TestBuildKernelFrameUsesIngressMonotonicTimestamp()
    {
        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(AssignSeq({
            Manifest(1),
            PadSample(9, true, true, false)
        }));

        const auto& stable = LastFrame(frames);
        const auto kernel = ingress::BuildKernelFrame(stable);
        Require(kernel.facts.monotonicUs == 200, "kernel monotonicUs must use ingress event time, not seq");
        Require(kernel.kernelRevision == stable.lastSeq, "kernel revision remains sequence-based");
    }
}

int main()
{
    TestHubAssignsSeqAndEmitsOverflowMarker();
    TestHubOverflowCompactsBoundaryFactsAndDropsVolatileInput();
    TestLegacySnapshotAdapterProducesControlSamplesAndPulseLedger();
    TestLegacySnapshotBatchOverflowRejectsPartialEvents();
    TestRejectedLegacySnapshotAdvancesWatermarkAsDroppedRange();
    TestLegacySequenceDiscontinuityProducesSequenceGap();
    TestLiveHidMaskEdgesProducePulseLedger();
    TestLiveHidPressSampleTriggersInteractionEngine();
    TestManifestPublisherProducesIngressMarker();
    TestDeviceFamilyProducerProducesMarkerAndPairedSourceEvidence();
    TestLiveGamepadInputPublishesSourceEvidence();
    TestLiveKeyboardMouseEvidencePublishesTakeoverAndReclaim();
    TestSyntheticKeyboardWindowDoesNotPublishKeyboardMouseTakeover();
    TestStableMergeKeepsPulseLedger();
    TestBoundaryChangeFlushesStableThenTransition();
    TestRecoveryMarkersMapFailClosed();
    TestFrameAssemblerDoesNotSortOutOfOrderEvents();
    TestFrameAssemblerRejectsMonotonicTimeRegression();
    TestFrameAssemblerOverflowPayloadBuildsBoundaryBaseline();
    TestDeviceMarkerMismatchFailsClosed();
    TestBuildKernelFrameDoesNotAcceptTransition();
    TestBuildKernelFrameUsesIngressMonotonicTimestamp();
    std::cout << "DualPadIngressTests passed\n";
    return 0;
}
