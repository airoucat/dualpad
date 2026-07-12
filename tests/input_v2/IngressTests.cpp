#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/ingress/LegacyIngressAdapter.h"
#include "input_v2/ingress/LiveInputFactProducer.h"
#include "input_v2/ingress/IngressRecovery.h"
#include "input_v2/ingress/GamepadActivityClassifier.h"
#include "input_v2/ingress/KbmGameplayFacts.h"
#include "input_v2/ingress/KbmGameplayFactProducer.h"
#include "input_v2/runtime/InputRecovery.h"
#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/config/ActionManifestPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"

#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
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

    presentation::SourceEvidenceFrame GamepadSourceFrame(std::uint32_t revision, std::uint64_t tick, bool changed)
    {
        presentation::SourceEvidenceFrame frame{};
        if (changed) {
            frame.records.push_back(presentation::SourceEvidenceRecord{
                .kind = presentation::SourceEvidenceRecordKind::DeviceFamilyChanged,
                .deviceFamilyChanged = presentation::DeviceFamilyChangedPayload{
                    .family = presentation::DeviceFamily::Gamepad,
                    .newRevision = revision,
                    .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
                    .publishedTick = tick
                }
            });
        }
        frame.records.push_back(presentation::SourceEvidenceRecord{
            .kind = presentation::SourceEvidenceRecordKind::SourceEvidenceSnapshot,
            .sourceEvidence = presentation::SourceEvidenceSnapshot{
                .deviceFamilyEvidence = presentation::PublishedDeviceFamilyEvidence{
                    .family = presentation::DeviceFamily::Gamepad,
                    .deviceFamilyRevision = revision,
                    .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
                    .publishedTick = tick
                },
                .gamepadEvidence = true,
                .gamepadLease = true,
                .collectedTick = tick
            }
        });
        return frame;
    }

    presentation::SourceEvidenceFrame KeyboardMouseSourceFrame(
        std::uint32_t revision,
        std::uint64_t tick,
        bool changed)
    {
        presentation::SourceEvidenceFrame frame{};
        if (changed) {
            frame.records.push_back(presentation::SourceEvidenceRecord{
                .kind = presentation::SourceEvidenceRecordKind::DeviceFamilyChanged,
                .deviceFamilyChanged = presentation::DeviceFamilyChangedPayload{
                    .family = presentation::DeviceFamily::KeyboardMouse,
                    .newRevision = revision,
                    .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
                    .publishedTick = tick
                }
            });
        }
        frame.records.push_back(presentation::SourceEvidenceRecord{
            .kind = presentation::SourceEvidenceRecordKind::SourceEvidenceSnapshot,
            .sourceEvidence = presentation::SourceEvidenceSnapshot{
                .deviceFamilyEvidence = presentation::PublishedDeviceFamilyEvidence{
                    .family = presentation::DeviceFamily::KeyboardMouse,
                    .deviceFamilyRevision = revision,
                    .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
                    .publishedTick = tick
                },
                .keyboardEvidence = true,
                .collectedTick = tick
            }
        });
        return frame;
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

    const actions::ControlSample* FindControlSample(
        const ingress::FactFrame& facts,
        std::uint32_t code)
    {
        for (const auto& sample : facts.controlSamples) {
            if (sample.path.kind == actions::ControlPathKind::DigitalButton &&
                sample.path.code == code) {
                return &sample;
            }
        }
        return nullptr;
    }

    const actions::ControlSample* FindAxisSample(
        const ingress::FactFrame& facts,
        input::PadAxisId axis)
    {
        for (const auto& sample : facts.controlSamples) {
            if (sample.path.kind == actions::ControlPathKind::AnalogAxis1D &&
                sample.path.code == static_cast<std::uint32_t>(axis)) {
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

    void TestHubDrainHonorsExactEventBudget()
    {
        ingress::IngressHub hub{ 32 };
        for (std::uint32_t epoch = 1; epoch <= 17; ++epoch) {
            Require(hub.PushEvent(Manifest(epoch)), "budget fixture event must enqueue");
        }

        const auto none = hub.Drain(0);
        Require(none.empty(), "zero event budget must not consume ingress");
        Require(hub.PendingCount() == 17, "zero event budget must preserve all pending events");

        const auto first = hub.Drain(16);
        Require(first.size() == 16, "16-event budget must consume exactly 16 events");
        Require(first.front().seq == 1 && first.back().seq == 16, "bounded drain must preserve event order");
        Require(hub.PendingCount() == 1, "17th event must remain pending after 16-event drain");

        const auto second = hub.Drain(16);
        Require(second.size() == 1 && second.front().seq == 17, "next drain must return the retained 17th event");
        Require(hub.PendingCount() == 0, "second drain must empty the fixture");
    }

    void BatchApiCompileFixture()
    {
        ingress::IngressHub hub{ 16 };

        ingress::ClassifiedGamepadReportDraft gamepad{};
        gamepad.current.sourceSequence = 11;
        gamepad.current.sourceTimestampUs = 1100;
        const auto gamepadReceipt = hub.PublishGamepadBatch(
            std::move(gamepad),
            ingress::GamepadConnectionDraft{
                .connectivity = ingress::GamepadConnectivity::Connected
            });
        Require(gamepadReceipt.accepted, "empty gamepad scaffold batch must publish atomically");

        ingress::OwnerKbmIngressDraft ownerKbm{};
        ownerKbm.boundary.contextRevision = 7;
        ownerKbm.boundary.menuStackRevision = 9;
        ownerKbm.boundary.controlMapFingerprint = 0x1234;
        ownerKbm.boundary.bindingGeneration = 3;
        ownerKbm.kbm.completeCurrent.complete = true;
        ownerKbm.kbm.physical.complete = true;
        ownerKbm.kbm.orderedEdges.push_back(ingress::KbmGameplayEdgeDraft{
            .eventOrdinal = 17,
            .physical = { ingress::KbmPhysicalDevice::Keyboard, 0x72 },
            .gameplayClass = ingress::KbmGameplayClass::SustainedDigital,
            .actionId = "Game.Sprint",
            .phase = ingress::KbmEdgePhase::Press,
            .origin = ingress::KbmEdgeOrigin::Physical
        });
        const auto kbmReceipt = hub.PublishOwnerKbmBatch(ownerKbm);
        Require(kbmReceipt.accepted, "empty KBM scaffold batch must publish atomically");

        const auto capture = hub.Capture(16);
        Require(capture.latestPadState.has_value(), "gamepad batch must expose the existing latest pad slot");
        Require(capture.latestGamepadConnection.has_value(), "gamepad connection slot must compile and capture");
        Require(capture.latestKbmGameplay.has_value(), "KBM latest slot must compile and capture");
        Require(kbmReceipt.controlMapRevision == 1, "owner KBM batch must advance the fingerprint revision in its transaction");
        Require(
            capture.latestKbmGameplay->causal.controlMapRevision == kbmReceipt.controlMapRevision,
            "KBM latest and receipt must share the transaction control-map revision");
        Require(capture.latestKbmGameplay->keyboardSustainedEventOrdinal == 17,
            "KBM latest must retain the earliest physical Sprint ordinal from the owner batch");
        Require(capture.inputStateEpoch == kbmReceipt.inputStateEpoch, "receipt and capture must expose one epoch");
        Require(capture.gamepadSessionId == gamepadReceipt.gamepadSessionId, "receipt and capture must expose one session");

        const ingress::InputFactCoherenceKey coherence{
            .captureGeneration = capture.generation,
            .orderedCutoffSeq = capture.orderedCutoffSeq,
            .inputStateEpoch = capture.inputStateEpoch,
            .gamepadSessionId = capture.gamepadSessionId,
            .contextRevision = kbmReceipt.contextRevision,
            .menuStackRevision = ownerKbm.boundary.menuStackRevision,
            .controlMapRevision = kbmReceipt.controlMapRevision
        };
        Require(coherence.captureGeneration != 0, "coherence key scaffold must be constructible");

        ingress::FrameAssembler assembler;
        const auto shadowFrames = assembler.Assemble(capture);
        Require(!shadowFrames.empty(), "coherent batch capture must assemble at least one boundary or stable frame");
        Require(
            std::all_of(shadowFrames.begin(), shadowFrames.end(), [&](const ingress::AssembledFactFrame& frame) {
                return frame.facts.coherence.captureGeneration == capture.generation;
            }),
            "capture overload must stamp shadow coherence metadata");
        Require(
            std::any_of(shadowFrames.begin(), shadowFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.kind == ingress::AssembledFrameKind::Stable && frame.facts.kbmGameplay.has_value();
            }),
            "KBM latest must publish only after its boundary marker reaches the cumulative cutoff");
        Require(
            std::any_of(shadowFrames.begin(), shadowFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.kind == ingress::AssembledFrameKind::Stable &&
                    frame.facts.gamepadConnection &&
                    frame.facts.gamepadConnection->connectivity == ingress::GamepadConnectivity::Connected;
            }),
            "context/control-map epoch changes must not erase independent gamepad connectivity");

        const auto resetReceipt = hub.PublishGlobalReset(
            ingress::ToMask(ingress::InputResetReason::ExplicitReset),
            ingress::InputResetScope::GlobalInputState);
        Require(resetReceipt.accepted, "global reset scaffold must publish an ordered marker");
        const auto resetCapture = hub.Capture(16);
        ingress::FrameAssembler resetAssembler;
        const auto resetFrames = resetAssembler.Assemble(resetCapture);
        Require(
            std::none_of(resetFrames.begin(), resetFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return ingress::ShouldDispatchToInteractionEngine(frame);
            }),
            "global reset marker must fail closed instead of producing a stable gameplay frame");
    }

    void BatchCapacityAtomicityFixture()
    {
        ingress::IngressHub hub{ 1 };
        ingress::ClassifiedGamepadReportDraft report{};
        report.current.sourceSequence = 1;
        report.current.sourceTimestampUs = 100;
        report.current.state.leftStick.x = 0.75F;
        report.sourceActivities = {
            ingress::MeaningfulSourceActivityDraft{
                .source = ingress::PhysicalInputSource::Gamepad,
                .kind = ingress::SourceActivityKind::GamepadButtonPress,
                .controlCode = 1
            },
            ingress::MeaningfulSourceActivityDraft{
                .source = ingress::PhysicalInputSource::Gamepad,
                .kind = ingress::SourceActivityKind::GamepadButtonPress,
                .controlCode = 2
            }
        };

        const auto receipt = hub.PublishGamepadBatch(
            std::move(report),
            ingress::GamepadConnectionDraft{
                .connectivity = ingress::GamepadConnectivity::Connected
            });
        Require(!receipt.accepted, "insufficient batch capacity must reject semantic publication");

        const auto capture = hub.Capture(8);
        Require(capture.events.size() == 1, "rejected batch must publish only one overflow marker");
        Require(capture.events.front().kind == ingress::IngressKind::QueueOverflow, "rejected batch must not leak ordered semantic records");
        Require(!capture.latestGamepadConnection.has_value(), "rejected batch must not half-commit connection semantics");
        Require(capture.latestPadState.has_value(), "overflow may retain complete physical pad current-state");
        Require(!capture.latestPadState->virtualGameplayEligible, "overflow-retained physical state must be virtual-ineligible");

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(capture);
        Require(
            std::none_of(frames.begin(), frames.end(), [](const ingress::AssembledFactFrame& frame) {
                return ingress::ShouldDispatchToInteractionEngine(frame);
            }),
            "overflow-retained physical state must not manufacture virtual gameplay");
    }

    void CumulativeEmptyCaptureFixture()
    {
        ingress::IngressHub hub{ 16 };
        std::vector<ingress::IngressEvent> events(7);
        Require(hub.PushEvents(std::move(events)), "seven ordered records must fit");

        const auto drained = hub.Capture(7);
        Require(drained.events.size() == 7, "fixture must drain through ordered seq 7");
        Require(drained.orderedCutoffSeq == 7, "drained cutoff must reach seq 7");

        const auto firstEmpty = hub.Capture(7);
        const auto secondEmpty = hub.Capture(7);
        Require(firstEmpty.events.empty() && secondEmpty.events.empty(), "follow-up captures must be empty");
        Require(firstEmpty.orderedCutoffSeq == 7, "first empty capture must preserve cumulative cutoff");
        Require(secondEmpty.orderedCutoffSeq == 7, "second empty capture must preserve cumulative cutoff");
    }

    void PartialDrainCausalTailFixture()
    {
        ingress::IngressHub hub{ 16 };
        ingress::ClassifiedGamepadReportDraft report{};
        report.current.sourceSequence = 1;
        report.current.sourceTimestampUs = 1000;
        report.current.state.connected = true;
        report.current.state.rightStick.x = 0.70F;
        report.orderedDigitalEdges = {
            { ingress::GamepadDigitalEdgePhase::Press, 0x1, 1, 1000 },
            { ingress::GamepadDigitalEdgePhase::Press, 0x2, 1, 1000 }
        };
        const auto receipt = hub.PublishGamepadBatch(
            std::move(report),
            ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected });
        Require(receipt.accepted && receipt.causalOrderedTailSeq == 2, "two-edge report must bind latest to ordered tail 2");

        ingress::FrameAssembler assembler;
        const auto first = hub.Capture(1);
        const auto firstFrames = assembler.Assemble(first);
        Require(first.orderedCutoffSeq == 1, "first partial capture must stop at cutoff 1");
        Require(
            std::none_of(firstFrames.begin(), firstFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.facts.latestPadStateGeneration != 0;
            }),
            "latest tail 2 must defer while cumulative cutoff is 1");

        const auto second = hub.Capture(1);
        const auto secondFrames = assembler.Assemble(second);
        const auto applied = std::find_if(secondFrames.begin(), secondFrames.end(), [](const ingress::AssembledFactFrame& frame) {
            return frame.kind == ingress::AssembledFrameKind::Stable && frame.facts.latestPadStateGeneration == 1;
        });
        Require(applied != secondFrames.end(), "latest must apply when cumulative cutoff reaches its causal tail");
        const auto* rightStick = FindAxisSample(applied->facts, input::PadAxisId::RightStickX);
        Require(rightStick && rightStick->scalar == 0.70F, "causally released latest must retain complete analog current-state");
    }

    void LatestOnlyNoFakeSeqFixture()
    {
        ingress::IngressHub hub{ 16 };
        ingress::ClassifiedGamepadReportDraft neutral{};
        neutral.current.state.connected = true;
        for (std::uint64_t sequence = 1; sequence <= 1000; ++sequence) {
            neutral.current.sourceSequence = sequence;
            neutral.current.sourceTimestampUs = sequence * 1000;
            Require(
                hub.PublishGamepadBatch(neutral, sequence == 1 ?
                    std::optional<ingress::GamepadConnectionDraft>{ ingress::GamepadConnectionDraft{
                        .connectivity = ingress::GamepadConnectivity::Connected } } :
                    std::nullopt).accepted,
                "latest-only neutral report must publish");
        }
        Require(hub.PendingCount() == 0, "latest-only reports must not allocate ordered records");

        ingress::ClassifiedGamepadReportDraft press{};
        press.current.sourceSequence = 1001;
        press.current.sourceTimestampUs = 1'001'000;
        press.current.state.connected = true;
        press.current.currentDownMask = 0x1;
        press.orderedDigitalEdges.push_back(
            { ingress::GamepadDigitalEdgePhase::Press, 0x1, 1001, 1'001'000 });
        const auto receipt = hub.PublishGamepadBatch(std::move(press), std::nullopt);
        Require(receipt.accepted, "ordered press after latest-only traffic must publish");
        Require(receipt.firstOrderedSeq == 1, "latest-only generations must not manufacture ingress sequence numbers");
    }

    void EmptyCaptureAppliesReadyLatestFixture()
    {
        ingress::IngressHub hub{ 16 };
        ingress::FrameAssembler assembler;
        Require(hub.PushEvent(PadSample(1, true, true, false)), "fixture ordered record must publish");
        (void)assembler.Assemble(hub.Capture(1));

        ingress::ClassifiedGamepadReportDraft latestOnly{};
        latestOnly.current.sourceSequence = 2;
        latestOnly.current.sourceTimestampUs = 2000;
        latestOnly.current.state.connected = true;
        latestOnly.current.state.leftStick.y = 0.50F;
        Require(hub.PublishGamepadBatch(std::move(latestOnly), std::nullopt).accepted, "latest-only state must publish");
        const auto empty = hub.Capture(0);
        Require(empty.events.empty() && empty.orderedCutoffSeq == 1, "empty capture must preserve cumulative cutoff 1");
        const auto frames = assembler.Assemble(empty);
        const auto stable = std::find_if(frames.begin(), frames.end(), [](const ingress::AssembledFactFrame& frame) {
            return frame.kind == ingress::AssembledFrameKind::Stable && frame.facts.latestPadStateGeneration != 0;
        });
        Require(stable != frames.end(), "empty capture must apply latest whose causal tail equals cumulative cutoff");
        const auto* axis = FindAxisSample(stable->facts, input::PadAxisId::LeftStickY);
        Require(axis && axis->scalar == 0.50F, "ready latest applied on empty capture must retain analog state");
    }

    void DisconnectScopeKeepsKbmAndGlobalEpochFixture()
    {
        ingress::IngressHub hub{ 16 };
        ingress::OwnerKbmIngressDraft kbm{};
        kbm.boundary.contextRevision = 7;
        kbm.boundary.menuStackRevision = 9;
        kbm.boundary.controlMapFingerprint = 0x100;
        kbm.boundary.bindingGeneration = 1;
        kbm.kbm.completeCurrent.complete = true;
        kbm.kbm.completeCurrent.keyboardMoveHeldMask = 0x1;
        kbm.kbm.completeCurrent.keyboardSustainedHeldMask = 0x1;
        kbm.kbm.physical.complete = true;
        Require(hub.PublishOwnerKbmBatch(std::move(kbm)).accepted, "KBM held baseline must publish");

        ingress::ClassifiedGamepadReportDraft connected{};
        connected.current.state.connected = true;
        connected.producerGamepadSessionId = 0;
        const auto connectedReceipt = hub.PublishGamepadBatch(
            std::move(connected),
            ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected });
        Require(connectedReceipt.accepted, "gamepad connection must publish");
        const auto connectedSnapshot = hub.GetGamepadConnectionSnapshot();
        Require(connectedSnapshot.connectivity == ingress::GamepadConnectivity::Connected &&
                connectedSnapshot.gamepadSessionId == connectedReceipt.gamepadSessionId,
            "read-only availability telemetry must observe the Hub-owned connected session");
        ingress::FrameAssembler assembler;
        (void)assembler.Assemble(hub.Capture(16));

        const auto disconnectedReceipt = hub.PublishGamepadDisconnect();
        const auto disconnected = hub.Capture(16);
        Require(disconnectedReceipt.accepted, "gamepad disconnect reset must publish");
        const auto disconnectedSnapshot = hub.GetGamepadConnectionSnapshot();
        Require(disconnectedSnapshot.connectivity == ingress::GamepadConnectivity::Disconnected &&
                disconnectedSnapshot.gamepadSessionId == disconnectedReceipt.gamepadSessionId,
            "read-only availability telemetry must observe the Hub-owned disconnect transition");
        Require(disconnected.inputStateEpoch == connectedReceipt.inputStateEpoch, "gamepad disconnect must not advance global input epoch");
        Require(disconnected.gamepadSessionId > connectedReceipt.gamepadSessionId, "gamepad disconnect must advance only gamepad session");
        Require(disconnected.latestKbmGameplay.has_value(), "gamepad disconnect must preserve KBM latest facts");
        Require(disconnected.latestKbmGameplay->current.keyboardMoveHeldMask == 0x1, "gamepad disconnect must preserve KBM Move held fact");
        Require(disconnected.latestKbmGameplay->current.keyboardSustainedHeldMask == 0x1, "gamepad disconnect must preserve KBM Sprint held fact");
        const auto frames = assembler.Assemble(disconnected);
        Require(
            std::any_of(frames.begin(), frames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.kind == ingress::AssembledFrameKind::Stable &&
                    frame.facts.kbmGameplay &&
                    frame.facts.kbmGameplay->current.keyboardMoveHeldMask == 0x1;
            }),
            "gamepad-scoped reset must keep KBM facts in the next coherent stable publication");
    }

    void OldGamepadSessionDropFixture()
    {
        ingress::IngressHub hub{ 16 };
        ingress::ClassifiedGamepadReportDraft connected{};
        connected.current.state.connected = true;
        connected.producerGamepadSessionId = 0;
        const auto connectedReceipt = hub.PublishGamepadBatch(
            std::move(connected),
            ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected });
        Require(connectedReceipt.accepted, "initial session must connect");
        const auto oldSession = connectedReceipt.gamepadSessionId;
        (void)hub.Capture(16);
        const auto disconnected = hub.PublishGamepadDisconnect();
        Require(disconnected.gamepadSessionId > oldSession, "disconnect must create a newer session");
        (void)hub.Capture(16);

        ingress::ClassifiedGamepadReportDraft stale{};
        stale.producerGamepadSessionId = oldSession;
        stale.current.sourceSequence = 9999;
        stale.current.sourceTimestampUs = 9999;
        stale.current.state.connected = true;
        stale.current.state.rightStick.x = 0.90F;
        stale.current.currentDownMask = 0x1;
        stale.orderedDigitalEdges.push_back(
            { ingress::GamepadDigitalEdgePhase::Press, 0x1, 9999, 9999 });
        const auto staleReceipt = hub.PublishGamepadBatch(std::move(stale), std::nullopt);
        Require(!staleReceipt.accepted, "old-session report must be rejected even with a larger generation");
        Require(staleReceipt.staleGamepadSession, "old-session rejection must be distinguishable from capacity overflow");
        const auto afterStale = hub.Capture(16);
        Require(afterStale.events.empty(), "old-session report must publish no ordered activity");
        Require(!afterStale.latestPadState.has_value(), "old-session report must not restore disconnected current-state");
        Require(afterStale.gamepadSessionId == disconnected.gamepadSessionId, "old-session report must not mutate current session");
    }

    void OverflowDoesNotReattachOldHeldFixture()
    {
        ingress::IngressHub hub{ 1 };
        ingress::FrameAssembler assembler;

        ingress::ClassifiedGamepadReportDraft held{};
        held.current.sourceSequence = 1;
        held.current.sourceTimestampUs = 1000;
        held.current.state.connected = true;
        held.current.state.leftStick.x = 0.60F;
        held.current.currentDownMask = 0x1;
        held.orderedDigitalEdges.push_back(
            { ingress::GamepadDigitalEdgePhase::Press, 0x1, 1, 1000 });
        Require(
            hub.PublishGamepadBatch(
                std::move(held),
                ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected }).accepted,
            "pre-overflow held baseline must publish");
        const auto baselineFrames = assembler.Assemble(hub.Capture(1));
        Require(
            std::any_of(baselineFrames.begin(), baselineFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.kind == ingress::AssembledFrameKind::Stable && !frame.facts.controlSamples.empty();
            }),
            "fixture must first establish held gamepad facts");

        ingress::ClassifiedGamepadReportDraft overflow{};
        overflow.current.sourceSequence = 2;
        overflow.current.sourceTimestampUs = 2000;
        overflow.current.state.connected = true;
        overflow.current.state.leftStick.x = 0.90F;
        overflow.current.currentDownMask = 0x1;
        overflow.orderedDigitalEdges = {
            { ingress::GamepadDigitalEdgePhase::Press, 0x2, 2, 2000 },
            { ingress::GamepadDigitalEdgePhase::Press, 0x4, 2, 2000 }
        };
        const auto overflowReceipt = hub.PublishGamepadBatch(std::move(overflow), std::nullopt);
        Require(!overflowReceipt.accepted, "oversized report must enter overflow recovery");
        const auto overflowFrames = assembler.Assemble(hub.Capture(1));
        Require(
            std::none_of(overflowFrames.begin(), overflowFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return ingress::ShouldDispatchToInteractionEngine(frame);
            }),
            "overflow capture must publish no virtual gameplay frame");

        ingress::OwnerKbmIngressDraft unrelated{};
        unrelated.kbm.sourceActivities.push_back(ingress::MeaningfulSourceActivityDraft{
            .source = ingress::PhysicalInputSource::Keyboard,
            .kind = ingress::SourceActivityKind::KeyboardPress,
            .controlCode = 0x20,
            .producerTimestampUs = 3000
        });
        Require(hub.PublishOwnerKbmBatch(std::move(unrelated)).accepted, "post-overflow unrelated KBM fact must publish");
        const auto nextFrames = assembler.Assemble(hub.Capture(1));
        for (const auto& frame : nextFrames) {
            if (frame.kind != ingress::AssembledFrameKind::Stable) {
                continue;
            }
            Require(
                FindAxisSample(frame.facts, input::PadAxisId::LeftStickX) == nullptr,
                "post-overflow stable frame must not reattach old gamepad analog held state");
            Require(
                std::none_of(frame.facts.controlSamples.begin(), frame.facts.controlSamples.end(), [](const actions::ControlSample& sample) {
                    return sample.path.kind == actions::ControlPathKind::DigitalButton && sample.path.code == 0x1 && sample.down;
                }),
                "post-overflow stable frame must not reattach old gamepad digital held state");
        }
    }

    void AtomicBoundaryFirstBatchFixture()
    {
        ingress::IngressHub hub{ 16 };
        ingress::OwnerKbmIngressDraft batch{};
        batch.boundary.contextRevision = 7;
        batch.boundary.menuStackRevision = 9;
        batch.boundary.controlMapFingerprint = 0xABCD;
        batch.boundary.bindingGeneration = 3;
        batch.kbm.ownerTickToken = 100;
        batch.kbm.eventBatchToken = 200;
        batch.kbm.completeCurrent.complete = true;
        batch.kbm.physical.complete = true;
        batch.kbm.orderedEdges.push_back(ingress::KbmGameplayEdgeDraft{
            .producerTimestampUs = 1000,
            .physical = { ingress::KbmPhysicalDevice::Keyboard, 0x20 },
            .gameplayClass = ingress::KbmGameplayClass::Move,
            .actionId = "Game.Move",
            .phase = ingress::KbmEdgePhase::Press,
            .origin = ingress::KbmEdgeOrigin::Physical
        });
        const auto receipt = hub.PublishOwnerKbmBatch(std::move(batch));
        Require(receipt.accepted, "new-boundary first KBM batch must publish atomically");

        ingress::FrameAssembler assembler;
        const auto boundaryCapture = hub.Capture(1);
        Require(boundaryCapture.events.size() == 1, "first partial capture must contain the boundary marker only");
        Require(boundaryCapture.events.front().kind == ingress::IngressKind::UiSnapshot, "new boundary must be ordered before its first KBM edge");
        Require(boundaryCapture.events.front().ui.contextRevision == 7, "boundary marker must carry ContextResolver revision");
        Require(boundaryCapture.events.front().ui.controlMapRevision == receipt.controlMapRevision, "boundary marker and receipt must share control-map revision");
        Require(boundaryCapture.events.front().ui.bindingGeneration == 3, "boundary marker must carry the callback binding generation");
        const auto boundaryFrames = assembler.Assemble(boundaryCapture);
        Require(
            std::none_of(boundaryFrames.begin(), boundaryFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.kind == ingress::AssembledFrameKind::Stable && frame.facts.kbmGameplay.has_value();
            }),
            "new-revision latest must wait while the first event remains beyond cutoff");

        const auto firstEventCapture = hub.Capture(1);
        Require(firstEventCapture.events.size() == 1, "second partial capture must contain first new-boundary KBM edge");
        Require(firstEventCapture.events.front().kind == ingress::IngressKind::KbmGameplayEdge, "first KBM edge must follow its boundary marker");
        Require(firstEventCapture.events.front().kbmGameplayEdge.controlMapRevision == receipt.controlMapRevision, "first KBM edge must use the new transaction revision");
        const auto eventFrames = assembler.Assemble(firstEventCapture);
        const auto stable = std::find_if(eventFrames.begin(), eventFrames.end(), [](const ingress::AssembledFactFrame& frame) {
            return frame.kind == ingress::AssembledFrameKind::Stable && frame.facts.kbmGameplay.has_value();
        });
        Require(stable != eventFrames.end(), "KBM latest must apply once its first event reaches cumulative cutoff");
        Require(stable->facts.kbmGameplay->bindingGeneration == 3, "KBM latest and boundary marker must share binding generation");
        Require(stable->boundaryKey.contextRevision == 7, "first new mapping event must not be rejected by old context revision");
        Require(stable->boundaryKey.controlMapRevision == receipt.controlMapRevision, "first new mapping event must share boundary control-map revision");

        ingress::ClassifiedGamepadReportDraft gamepad{};
        gamepad.current.sourceSequence = 1;
        gamepad.current.sourceTimestampUs = 2000;
        gamepad.current.state.connected = true;
        gamepad.current.state.leftStick.x = 0.65F;
        gamepad.producerGamepadSessionId = firstEventCapture.gamepadSessionId;
        Require(
            hub.PublishGamepadBatch(
                std::move(gamepad),
                ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected }).accepted,
            "context-neutral gamepad current-state must publish after KBM boundary");
        const auto gamepadFrames = assembler.Assemble(hub.Capture(0));
        const auto gamepadStable = std::find_if(gamepadFrames.begin(), gamepadFrames.end(), [](const ingress::AssembledFactFrame& frame) {
            return frame.kind == ingress::AssembledFrameKind::Stable &&
                FindAxisSample(frame.facts, input::PadAxisId::LeftStickX) != nullptr;
        });
        Require(gamepadStable != gamepadFrames.end(), "gamepad latest must inherit Hub context/menu boundary and remain causally applicable");
        Require(
            FindAxisSample(gamepadStable->facts, input::PadAxisId::LeftStickX)->scalar == 0.65F,
            "post-boundary gamepad latest must retain complete analog state");
    }

    void SequenceGapRequestsGlobalEpochResetFixture()
    {
        ingress::FrameAssembler assembler;
        auto first = PadSample(1, true, true, false);
        first.seq = 1;
        auto skipped = PadSample(2, true, true, false);
        skipped.seq = 3;
        const auto frames = assembler.Assemble({ first, skipped });
        Require(
            std::any_of(frames.begin(), frames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.kind == ingress::AssembledFrameKind::Transition &&
                    frame.transition.reason == ingress::TransitionReason::SequenceGap;
            }),
            "ordered ingress gap must immediately emit a fail-closed transition");
        const auto resetReasons = assembler.ConsumeGlobalResetRequest();
        Require(
            (resetReasons & ingress::ToMask(ingress::InputResetReason::SequenceGap)) != 0,
            "ordered ingress gap must request a Hub-owned global reset receipt");

        ingress::IngressHub hub{ 16 };
        const auto before = hub.Capture(0).inputStateEpoch;
        const auto receipt = hub.PublishGlobalReset(resetReasons, ingress::InputResetScope::GlobalInputState);
        Require(receipt.accepted && receipt.inputStateEpoch > before, "global sequence-gap receipt must advance Hub epoch");
        const auto capture = hub.Capture(1);
        Require(
            capture.events.size() == 1 &&
                capture.events.front().kind == ingress::IngressKind::InputReset &&
                capture.events.front().inputReset.inputStateEpoch == receipt.inputStateEpoch,
            "global sequence-gap reset marker must publish with the new epoch");
    }

    void RevisionAheadFixture()
    {
        ingress::LatestKbmGameplayFacts oldLatest{};
        oldLatest.causal.generation = 1;
        oldLatest.causal.causalOrderedTailSeq = 2;
        oldLatest.causal.inputStateEpoch = 2;
        oldLatest.causal.contextRevision = 1;
        oldLatest.causal.controlMapRevision = 1;
        oldLatest.current.complete = true;
        oldLatest.current.keyboardMoveHeldMask = 0x1;

        ingress::IngressEvent boundary{};
        boundary.seq = 1;
        boundary.kind = ingress::IngressKind::UiSnapshot;
        boundary.ui = ingress::UiSnapshotPayload{
            .contextRevision = 2,
            .menuStackRevision = 2,
            .controlMapRevision = 2,
            .bindingGeneration = 2
        };
        ingress::IngressCapture first{};
        first.generation = 1;
        first.events.push_back(boundary);
        first.orderedCutoffSeq = 1;
        first.inputStateEpoch = 2;
        first.latestKbmGameplay = oldLatest;

        ingress::FrameAssembler assembler;
        const auto firstFrames = assembler.Assemble(first);
        Require(
            std::none_of(firstFrames.begin(), firstFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.kind == ingress::AssembledFrameKind::Stable && frame.facts.kbmGameplay.has_value();
            }),
            "old-context latest ahead of boundary prefix must defer");

        ingress::IngressEvent tail{};
        tail.seq = 2;
        tail.kind = ingress::IngressKind::HostFacts;
        ingress::IngressCapture second{};
        second.generation = 2;
        second.events.push_back(tail);
        second.orderedCutoffSeq = 2;
        second.inputStateEpoch = 2;
        second.latestKbmGameplay = oldLatest;
        const auto secondFrames = assembler.Assemble(second);
        Require(
            std::none_of(secondFrames.begin(), secondFrames.end(), [](const ingress::AssembledFactFrame& frame) {
                return frame.facts.kbmGameplay.has_value();
            }),
            "old-context latest must drop rather than cross the new boundary when its tail becomes ready");
    }

    void NeutralHidInterleaveFixture()
    {
        constexpr std::array producerRates{ 500u, 1000u };
        constexpr std::array ownerRates{ 30u, 60u, 120u };

        for (const auto producerRate : producerRates) {
            for (const auto ownerRate : ownerRates) {
                ingress::IngressHub hub{ 256 };
                ingress::GamepadActivityClassifier classifier;
                input::PadState neutral{};
                neutral.connected = true;

                std::size_t gamepadActivities = 0;
                std::size_t keyboardMouseActivities = 0;
                const auto reportsPerOwnerTick = (producerRate + ownerRate - 1) / ownerRate;
                const auto reportCount = producerRate * 2;

                ingress::OwnerKbmIngressDraft initialKeyboard{};
                initialKeyboard.kbm.sourceActivities.push_back(ingress::MeaningfulSourceActivityDraft{
                    .source = ingress::PhysicalInputSource::Keyboard,
                    .kind = ingress::SourceActivityKind::KeyboardPress,
                    .controlCode = 0x1E,
                    .producerTimestampUs = 1
                });
                Require(hub.PublishOwnerKbmBatch(std::move(initialKeyboard)).accepted, "keyboard takeover must publish");

                for (std::uint32_t reportIndex = 1; reportIndex <= reportCount; ++reportIndex) {
                    auto report = classifier.Classify(neutral, neutral, reportIndex, reportIndex * 1000ull);
                    Require(report.meaningfulActivities.empty(), "neutral HID report must not create typed activity");
                    Require(report.sourceActivities.empty(), "neutral HID report must not create source activity");
                    Require(
                        hub.PublishGamepadBatch(
                            std::move(report),
                            reportIndex == 1 ?
                                std::optional<ingress::GamepadConnectionDraft>{ ingress::GamepadConnectionDraft{
                                    .connectivity = ingress::GamepadConnectivity::Connected
                                } } :
                                std::nullopt)
                            .accepted,
                        "neutral HID current-state must publish");

                    if ((reportIndex % reportsPerOwnerTick) == 0) {
                        ingress::OwnerKbmIngressDraft interleavedKbm{};
                        interleavedKbm.kbm.sourceActivities.push_back(ingress::MeaningfulSourceActivityDraft{
                            .source = (reportIndex / reportsPerOwnerTick) % 2 == 0 ?
                                ingress::PhysicalInputSource::Keyboard : ingress::PhysicalInputSource::Mouse,
                            .kind = (reportIndex / reportsPerOwnerTick) % 2 == 0 ?
                                ingress::SourceActivityKind::KeyboardPress : ingress::SourceActivityKind::MouseDelta,
                            .controlCode = 0x1E,
                            .deltaX = 1,
                            .producerTimestampUs = reportIndex * 1000ull + 1
                        });
                        Require(hub.PublishOwnerKbmBatch(std::move(interleavedKbm)).accepted, "interleaved KBM activity must publish");
                    }

                    if ((reportIndex % reportsPerOwnerTick) == 0 || reportIndex == reportCount) {
                        const auto capture = hub.Capture(256);
                        for (const auto& event : capture.events) {
                            if (event.kind != ingress::IngressKind::MeaningfulSourceActivity) {
                                continue;
                            }
                            if (event.sourceActivity.source == ingress::PhysicalInputSource::Gamepad) {
                                ++gamepadActivities;
                            } else {
                                ++keyboardMouseActivities;
                            }
                        }
                        Require(capture.latestPadState.has_value(), "neutral reports must keep complete current-state visible");
                    }
                }

                const auto latest = hub.Capture(0);
                Require(latest.latestPadState.has_value(), "neutral rate matrix must retain latest pad state");
                Require(latest.latestPadState->generation == reportCount, "every neutral report must advance current-state generation");
                Require(gamepadActivities == 0, "neutral reports must never steal source ownership");
                Require(keyboardMouseActivities != 0, "interleaved KBM source activities must retain Hub ordering");
            }
        }
    }

    void HeldStickUnchangedFixture()
    {
        ingress::IngressHub hub{ 256 };
        ingress::GamepadActivityClassifier classifier;
        input::PadState neutral{};
        input::PadState held{};
        held.connected = true;
        held.rightStick.x = 0.60F;

        auto entered = classifier.Classify(neutral, held, 1, 1000);
        Require(entered.meaningfulActivities.size() == 1, "right stick enter must emit one typed activity");
        Require(
            entered.meaningfulActivities.front().reason == ingress::GamepadActivityReason::RightStickEntered,
            "right stick enter must use RightStickEntered reason");
        Require(entered.sourceActivities.size() == 1, "right stick enter must emit one context-neutral source activity");
        Require(
            hub.PublishGamepadBatch(
                std::move(entered),
                ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected })
                .accepted,
            "right stick enter batch must publish");
        (void)hub.Capture(256);

        for (std::uint64_t sequence = 2; sequence <= 121; ++sequence) {
            auto unchanged = classifier.Classify(held, held, sequence, sequence * 1000);
            Require(unchanged.meaningfulActivities.empty(), "unchanged held stick must not repeat typed activity");
            Require(unchanged.sourceActivities.empty(), "unchanged held stick must not repeat source activity");
            Require(hub.PublishGamepadBatch(std::move(unchanged), std::nullopt).accepted, "unchanged held state must publish");
        }

        const auto capture = hub.Capture(256);
        Require(capture.events.empty(), "unchanged held reports must not amplify ordered queue");
        Require(capture.latestPadState.has_value(), "held stick must remain available as latest current-state");
        Require(capture.latestPadState->generation == 121, "held current-state generation must advance for every report");
        Require(capture.latestPadState->state.rightStick.x == 0.60F, "held right stick current-state must not disappear");
    }

    void ReleaseDoesNotTakeoverFixture()
    {
        ingress::GamepadActivityClassifier classifier;
        input::PadState held{};
        held.connected = true;
        held.buttons.digitalMask = 0x1;
        held.rightStick.x = 0.60F;
        input::PadState released{};
        released.connected = true;

        auto report = classifier.Classify(held, released, 2, 2000);
        Require(report.orderedDigitalEdges.size() == 1, "button release must remain an ordered edge");
        Require(
            report.orderedDigitalEdges.front().phase == ingress::GamepadDigitalEdgePhase::Release,
            "button release must retain release phase");
        Require(report.meaningfulActivities.empty(), "release and stick return must not create typed activity");
        Require(report.sourceActivities.empty(), "release and stick return must not create source takeover");

        ingress::IngressHub hub{ 16 };
        Require(
            hub.PublishGamepadBatch(
                std::move(report),
                ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected })
                .accepted,
            "release current-state must publish");
        const auto capture = hub.Capture(16);
        Require(capture.events.size() == 1, "release batch must publish only its ordered digital edge");
        Require(capture.latestPadState && capture.latestPadState->currentDownMask == 0, "release must clear current down mask");
        Require(capture.latestPadState->state.rightStick.x == 0.0F, "stick return must update complete current-state");
    }

    void ConnectivityOnlyFixture()
    {
        ingress::IngressHub hub{ 16 };
        ingress::GamepadActivityClassifier classifier;
        input::PadState connected{};
        connected.connected = true;

        auto report = classifier.Classify({}, connected, 1, 1000);
        const auto connectedReceipt = hub.PublishGamepadBatch(
            std::move(report),
            ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected });
        Require(connectedReceipt.accepted, "connectivity-only connect must publish");
        const auto connectedCapture = hub.Capture(16);
        Require(connectedCapture.events.empty(), "connect must not create ordered source activity");
        Require(!connectedCapture.latestSourceEvidence.has_value(), "connect must not mutate presentation evidence");

        const auto disconnectedReceipt = hub.PublishGamepadDisconnect();
        Require(disconnectedReceipt.accepted, "disconnect must publish");
        const auto disconnectedCapture = hub.Capture(16);
        Require(
            disconnectedCapture.events.size() == 1 &&
                disconnectedCapture.events.front().kind == ingress::IngressKind::InputReset &&
                disconnectedCapture.events.front().inputReset.scope == ingress::InputResetScope::GamepadSource,
            "disconnect must publish one gamepad-scoped reset without source activity");
        Require(disconnectedCapture.gamepadSessionId > connectedCapture.gamepadSessionId, "disconnect must advance gamepad session");
        Require(!disconnectedCapture.latestPadState.has_value(), "disconnect must clear gamepad current-state");
        Require(
            disconnectedCapture.latestGamepadConnection &&
                disconnectedCapture.latestGamepadConnection->connectivity == ingress::GamepadConnectivity::Disconnected,
            "disconnect must publish disconnected connection facts");

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(disconnectedCapture);
        const auto transition = std::find_if(frames.begin(), frames.end(), [](const auto& frame) {
            return frame.kind == ingress::AssembledFrameKind::Transition &&
                frame.transition.reason == ingress::TransitionReason::ExplicitReset;
        });
        Require(transition != frames.end(), "disconnect reset must assemble a transition frame");
        Require(transition->transition.hasResetScope &&
                transition->transition.resetScope == ingress::InputResetScope::GamepadSource,
            "assembled disconnect transition must retain GamepadSource scope");
        const auto recovery = ingress::ToGameplayRecoveryInput(*transition);
        Require(recovery.resetScope == gameplay::RecoveryResetScope::GamepadSource,
            "runtime recovery must preserve GamepadSource scope instead of widening to Global");
    }

    void ContextNeutralGamepadDraftFixture()
    {
        ingress::GamepadActivityClassifier classifier;
        input::PadState previous{};
        input::PadState current{};
        current.connected = true;
        current.buttons.digitalMask = 0x4;
        current.leftStick.y = 0.50F;

        const auto gameplayDraft = classifier.Classify(previous, current, 7, 7000);
        const auto menuDraft = classifier.Classify(previous, current, 7, 7000);
        Require(gameplayDraft.orderedDigitalEdges.size() == menuDraft.orderedDigitalEdges.size(), "raw edge count must be context-neutral");
        Require(gameplayDraft.meaningfulActivities.size() == menuDraft.meaningfulActivities.size(), "typed activity count must be context-neutral");
        Require(gameplayDraft.sourceActivities.size() == menuDraft.sourceActivities.size(), "source activity count must be context-neutral");
        for (std::size_t index = 0; index < gameplayDraft.meaningfulActivities.size(); ++index) {
            Require(
                gameplayDraft.meaningfulActivities[index].reason == menuDraft.meaningfulActivities[index].reason &&
                    gameplayDraft.meaningfulActivities[index].controlCode == menuDraft.meaningfulActivities[index].controlCode,
                "classifier must emit identical raw activity independent of owner context");
        }
    }

    void ClassifiedDigitalEdgeFeedsExistingKernelFixture()
    {
        ingress::GamepadActivityClassifier classifier;
        input::PadState previous{};
        input::PadState pressed{};
        pressed.connected = true;
        pressed.buttons.digitalMask = 0x1;

        ingress::IngressHub hub{ 16 };
        Require(
            hub.PublishGamepadBatch(
                classifier.Classify(previous, pressed, 1, 1000),
                ingress::GamepadConnectionDraft{ .connectivity = ingress::GamepadConnectivity::Connected })
                .accepted,
            "classified digital press must publish");
        const auto capture = hub.Capture(16);
        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(capture);
        const auto stable = std::find_if(frames.begin(), frames.end(), [](const ingress::AssembledFactFrame& frame) {
            return frame.kind == ingress::AssembledFrameKind::Stable;
        });
        Require(stable != frames.end(), "classified digital press must assemble a stable frame");
        Require(stable->facts.pulseLedger.size() == 1, "classified digital press must reach the existing pulse ledger");
        Require(stable->facts.pulseLedger.front().pressed, "classified digital press must retain press phase");
        Require(stable->facts.pulseLedger.front().path.code == 0x1, "classified digital press must retain raw control code");
    }

    void OrderedMeaningfulActivityReachesOneStableFrameFixture()
    {
        ingress::IngressHub hub{ 16 };
        ingress::FrameAssembler assembler;
        ingress::ClassifiedGamepadReportDraft report{};
        report.current.sourceSequence = 1;
        report.current.sourceTimestampUs = 1000;
        report.current.state.connected = true;
        report.sourceActivities.push_back(ingress::MeaningfulSourceActivityDraft{
            .source = ingress::PhysicalInputSource::Gamepad,
            .kind = ingress::SourceActivityKind::GamepadButtonPress,
            .controlCode = 0x1,
            .producerTimestampUs = 1000
        });
        Require(hub.PublishGamepadBatch(
                    std::move(report),
                    ingress::GamepadConnectionDraft{
                        .connectivity = ingress::GamepadConnectivity::Connected })
                    .accepted,
            "ordered activity fixture must publish");
        const auto frames = assembler.Assemble(hub.Capture(16));
        const auto& stable = LastStableFrame(frames);
        Require(stable.facts.sourceActivities.size() == 1 &&
                stable.facts.sourceActivities.front().ingressSeq == stable.lastSeq &&
                stable.facts.sourceActivities.front().kind ==
                    ingress::SourceActivityKind::GamepadButtonPress,
            "ordered meaningful activity must reach the causal stable frame with ingress seq intact");

        ingress::ClassifiedGamepadReportDraft neutral{};
        neutral.current.sourceSequence = 2;
        neutral.current.sourceTimestampUs = 2000;
        neutral.current.state.connected = true;
        Require(hub.PublishGamepadBatch(std::move(neutral), std::nullopt).accepted,
            "neutral latest-only state must publish after activity");
        const auto neutralFrames = assembler.Assemble(hub.Capture(16));
        const auto& neutralStable = LastStableFrame(neutralFrames);
        Require(neutralStable.facts.sourceActivities.empty(),
            "ordered activity must not leak into a later neutral stable frame");
    }

    ingress::KbmBindingSnapshot FakeKbmBindingSnapshot(
        std::uint64_t generation = 1,
        std::uint32_t controlMapRevision = 5)
    {
        return ingress::KbmBindingSnapshot{
            .generation = generation,
            .controlMapRevision = controlMapRevision,
            .contextRevision = 7,
            .entries = {
                { { ingress::KbmPhysicalDevice::Keyboard, 0x70 }, "Game.Move", ingress::KbmGameplayClass::Move, 0x1 },
                { { ingress::KbmPhysicalDevice::Mouse, 8 }, "Game.Attack", ingress::KbmGameplayClass::Combat, 0x2 },
                { { ingress::KbmPhysicalDevice::Keyboard, 0x71 }, "Game.Jump", ingress::KbmGameplayClass::TransientDigital, 0x4 },
                { { ingress::KbmPhysicalDevice::Mouse, 9 }, "Game.Activate", ingress::KbmGameplayClass::TransientDigital, 0x8 },
                { { ingress::KbmPhysicalDevice::Keyboard, 0x72 }, "Game.Sprint", ingress::KbmGameplayClass::SustainedDigital, 0x10 },
                { { ingress::KbmPhysicalDevice::Mouse, ingress::kMouseDeltaPhysicalIdCode }, "Game.Look", ingress::KbmGameplayClass::Look, 0 }
            }
        };
    }

    ingress::KbmObservedBatch FakeObservedKbmBatch(
        std::uint64_t token,
        std::vector<ingress::KbmObservedEventDraft> events,
        std::initializer_list<ingress::KbmPhysicalCode> downCodes)
    {
        ingress::KbmObservedBatch observed{
            .ownerTickToken = token,
            .eventBatchToken = token,
            .events = std::move(events),
            .eventListComplete = true
        };
        observed.rawCurrent.providerGeneration = token;
        observed.rawCurrent.contextRevision = 7;
        observed.rawCurrent.controlMapRevision = 5;
        observed.rawCurrent.physicalOnlyProvenance = true;
        observed.rawCurrent.complete = true;
        for (const auto code : downCodes) {
            Require(observed.rawCurrent.downCodes.Insert(code), "fake raw code set must fit");
        }
        return observed;
    }

    ingress::KbmObservedEventDraft FakeKbmEvent(
        std::uint32_t ordinal,
        ingress::KbmPhysicalCode code,
        ingress::KbmEdgePhase phase,
        bool initialPress = true)
    {
        return ingress::KbmObservedEventDraft{
            .eventOrdinal = ordinal,
            .producerTimestampUs = ordinal * 1000ull,
            .physical = code,
            .phase = phase,
            .origin = ingress::KbmEdgeOrigin::Physical,
            .initialPress = initialPress
        };
    }

    void KbmProducerPublishesMappedCurrentAndOrderedFactsFixture()
    {
        ingress::KbmGameplayFactProducer producer;
        const auto bindings = FakeKbmBindingSnapshot();
        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 7;

        const ingress::KbmPhysicalCode move{ ingress::KbmPhysicalDevice::Keyboard, 0x70 };
        const ingress::KbmPhysicalCode combat{ ingress::KbmPhysicalDevice::Mouse, 8 };
        const ingress::KbmPhysicalCode sprint{ ingress::KbmPhysicalDevice::Keyboard, 0x72 };
        const ingress::KbmPhysicalCode mouseLook{
            ingress::KbmPhysicalDevice::Mouse,
            ingress::kMouseDeltaPhysicalIdCode
        };

        auto moveBatch = producer.BuildIngressBatch(
            FakeObservedKbmBatch(1, { FakeKbmEvent(1, move, ingress::KbmEdgePhase::Press) }, { move }),
            bindings,
            contextSnapshot,
            1000);
        Require(moveBatch.completeCurrent.keyboardMoveHeldMask == 0x1, "non-WASD mapped move press must set move current mask");
        Require(moveBatch.orderedEdges.size() == 1, "move press must publish one ordered edge");

        auto heldRepeatBatch = producer.BuildIngressBatch(
            FakeObservedKbmBatch(
                2,
                { FakeKbmEvent(2, move, ingress::KbmEdgePhase::Press, false) },
                { move }),
            bindings,
            contextSnapshot,
            2000);
        Require(heldRepeatBatch.orderedEdges.empty(), "held keyboard repeat must not manufacture another ordered press");
        Require(heldRepeatBatch.sourceActivities.empty(), "held keyboard repeat must not refresh meaningful KBM activity");
        Require(heldRepeatBatch.completeCurrent.keyboardMoveHeldMask == 0x1, "held keyboard repeat must preserve physical current-state");

        auto combatBatch = producer.BuildIngressBatch(
            FakeObservedKbmBatch(3, { FakeKbmEvent(3, combat, ingress::KbmEdgePhase::Press) }, { move, combat }),
            bindings,
            contextSnapshot,
            3000);
        Require(combatBatch.completeCurrent.keyboardMoveHeldMask == 0x1, "move held must survive mouse combat press");
        Require(combatBatch.completeCurrent.mouseCombatHeldMask == 0x2, "non-default mouse combat must set combat current mask");

        auto sprintBatch = producer.BuildIngressBatch(
            FakeObservedKbmBatch(3, { FakeKbmEvent(3, sprint, ingress::KbmEdgePhase::Press) }, { move, combat, sprint }),
            bindings,
            contextSnapshot,
            3000);
        Require(sprintBatch.completeCurrent.keyboardSustainedHeldMask == 0x10, "mapped Sprint press must set sustained current mask");
        Require(sprintBatch.orderedEdges.size() == 1 &&
                sprintBatch.orderedEdges.front().eventOrdinal == 3,
            "mapped Sprint edge must preserve callback-local ordinal for contributor ordering");

        auto mouseMove = FakeKbmEvent(4, mouseLook, ingress::KbmEdgePhase::MouseDelta);
        mouseMove.deltaX = 5;
        mouseMove.deltaY = -2;
        auto lookBatch = producer.BuildIngressBatch(
            FakeObservedKbmBatch(4, { mouseMove }, { move, combat, sprint }),
            bindings,
            contextSnapshot,
            4000);
        Require(lookBatch.orderedEdges.size() == 1, "physical mouse delta must remain ordered");
        Require(lookBatch.sourceActivities.size() == 1, "physical mouse delta must create one source activity");

        auto releasedBatch = producer.BuildIngressBatch(
            FakeObservedKbmBatch(
                5,
                {
                    FakeKbmEvent(5, move, ingress::KbmEdgePhase::Release, false),
                    FakeKbmEvent(6, combat, ingress::KbmEdgePhase::Release, false),
                    FakeKbmEvent(7, sprint, ingress::KbmEdgePhase::Release, false)
                },
                {}),
            bindings,
            contextSnapshot,
            5000);
        Require(releasedBatch.completeCurrent.keyboardMoveHeldMask == 0, "move release must clear current mask");
        Require(releasedBatch.completeCurrent.mouseCombatHeldMask == 0, "combat release must clear current mask");
        Require(releasedBatch.completeCurrent.keyboardSustainedHeldMask == 0, "Sprint release must clear sustained mask");
        Require(releasedBatch.orderedEdges.size() == 3, "all releases must remain ordered");

        ingress::KbmGameplayFactProducer rebuilt;
        const auto rebuiltBatch = rebuilt.BuildIngressBatch(
            FakeObservedKbmBatch(6, {}, { move, combat, sprint }),
            bindings,
            contextSnapshot,
            6000);
        Require(rebuiltBatch.completeCurrent.keyboardMoveHeldMask == 0x1, "producer rebuild must recover move from trusted raw state");
        Require(rebuiltBatch.completeCurrent.mouseCombatHeldMask == 0x2, "producer rebuild must recover combat from trusted raw state");
        Require(rebuiltBatch.completeCurrent.keyboardSustainedHeldMask == 0x10, "producer rebuild must recover Sprint from trusted raw state");
    }

    void KbmSyntheticSuppressionRequiresExactProvenanceFixture()
    {
        const auto bindings = FakeKbmBindingSnapshot();
        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 7;
        const ingress::KbmPhysicalCode jump{ ingress::KbmPhysicalDevice::Keyboard, 0x71 };

        const auto runCase = [&](ingress::SyntheticProvenanceMode mode, bool expectSuppressed) {
            ingress::KbmGameplayFactProducer producer;
            producer.RegisterSyntheticSuppression(ingress::SyntheticKeyboardSuppressionToken{
                .token = 99,
                .scancode = 0x71,
                .expectedPhase = ingress::KbmEdgePhase::Press,
                .contextRevision = 7,
                .originatingOutputGeneration = 44,
                .helperInjectionSequence = 55,
                .provenanceMode = mode,
                .remainingMatches = 1,
                .expiresAtOwnerUs = 10'000
            });
            auto event = FakeKbmEvent(1, jump, ingress::KbmEdgePhase::Press);
            event.syntheticToken = 99;
            event.originatingOutputGeneration = 44;
            event.helperInjectionSequence = 55;
            const auto batch = producer.BuildIngressBatch(
                FakeObservedKbmBatch(1, { event }, expectSuppressed ?
                    std::initializer_list<ingress::KbmPhysicalCode>{} :
                    std::initializer_list<ingress::KbmPhysicalCode>{ jump }),
                bindings,
                contextSnapshot,
                1000);
            Require(
                batch.orderedEdges.empty() == expectSuppressed,
                "only exact verified/reserved synthetic receipt may suppress ordered facts");
            Require(
                (batch.completeCurrent.keyboardTransientHeldMask == 0) == expectSuppressed,
                "unproven synthetic match must remain physical current-state");
        };

        runCase(ingress::SyntheticProvenanceMode::VerifiedPhysicalOnlyProvider, true);
        runCase(ingress::SyntheticProvenanceMode::ReservedNonCollidingControl, true);
        runCase(ingress::SyntheticProvenanceMode::Unproven, false);

        ingress::KbmGameplayFactProducer mismatchProducer;
        mismatchProducer.RegisterSyntheticSuppression(ingress::SyntheticKeyboardSuppressionToken{
            .token = 99,
            .scancode = 0x71,
            .expectedPhase = ingress::KbmEdgePhase::Press,
            .contextRevision = 7,
            .originatingOutputGeneration = 44,
            .helperInjectionSequence = 55,
            .provenanceMode = ingress::SyntheticProvenanceMode::ReservedNonCollidingControl,
            .remainingMatches = 1,
            .expiresAtOwnerUs = 10'000
        });
        auto mismatch = FakeKbmEvent(1, jump, ingress::KbmEdgePhase::Press);
        mismatch.syntheticToken = 98;
        mismatch.originatingOutputGeneration = 44;
        mismatch.helperInjectionSequence = 55;
        const auto mismatchBatch = mismatchProducer.BuildIngressBatch(
            FakeObservedKbmBatch(1, { mismatch }, { jump }),
            bindings,
            contextSnapshot,
            1000);
        Require(mismatchBatch.orderedEdges.size() == 1, "token mismatch must never swallow physical same-scancode input");
    }

    void KbmMappingChangeUsesStablePhysicalQuarantineFixture()
    {
        ingress::KbmGameplayFactProducer producer;
        auto bindings = FakeKbmBindingSnapshot(1, 5);
        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 7;
        const ingress::KbmPhysicalCode move{ ingress::KbmPhysicalDevice::Keyboard, 0x70 };

        (void)producer.BuildIngressBatch(
            FakeObservedKbmBatch(1, { FakeKbmEvent(1, move, ingress::KbmEdgePhase::Press) }, { move }),
            bindings,
            contextSnapshot,
            1000);

        bindings.generation = 2;
        bindings.controlMapRevision = 6;
        std::reverse(bindings.entries.begin(), bindings.entries.end());
        auto remapped = FakeObservedKbmBatch(2, {}, { move });
        remapped.rawCurrent.controlMapRevision = 6;
        const auto quarantined = producer.BuildIngressBatch(remapped, bindings, contextSnapshot, 2000);
        Require(quarantined.completeCurrent.keyboardMoveHeldMask == 0, "mapping change must clear old semantic held state");
        Require(quarantined.physical.quarantineCodes.Contains(move), "mapping change must quarantine stable physical identity");
        Require(quarantined.baseline == ingress::KbmBaselineState::MappingRearmRequired, "mapping change must require rearm");

        auto heldRepeat = FakeObservedKbmBatch(
            3,
            { FakeKbmEvent(3, move, ingress::KbmEdgePhase::Press, false) },
            { move });
        heldRepeat.rawCurrent.controlMapRevision = 6;
        const auto repeated = producer.BuildIngressBatch(heldRepeat, bindings, contextSnapshot, 3000);
        Require(repeated.completeCurrent.keyboardMoveHeldMask == 0, "held repeat must not rearm quarantined mapping");
        Require(repeated.physical.quarantineCodes.Contains(move), "held repeat must preserve quarantine identity after entry reorder");

        auto release = FakeObservedKbmBatch(
            4,
            { FakeKbmEvent(4, move, ingress::KbmEdgePhase::Release, false) },
            {});
        release.rawCurrent.controlMapRevision = 6;
        const auto released = producer.BuildIngressBatch(release, bindings, contextSnapshot, 4000);
        Require(!released.physical.quarantineCodes.Contains(move), "physical release must drain quarantine");
        Require(released.baseline == ingress::KbmBaselineState::Clean, "drained quarantine must restore clean baseline");
    }

    void KbmExplicitRecoveryQuarantinesAndResetsSyntheticFixture()
    {
        ingress::KbmGameplayFactProducer producer;
        const auto bindings = FakeKbmBindingSnapshot(1, 5);
        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 7;
        const ingress::KbmPhysicalCode move{ ingress::KbmPhysicalDevice::Keyboard, 0x70 };
        const ingress::KbmPhysicalCode jump{ ingress::KbmPhysicalDevice::Keyboard, 0x71 };

        (void)producer.BuildIngressBatch(
            FakeObservedKbmBatch(1, { FakeKbmEvent(1, move, ingress::KbmEdgePhase::Press) }, { move }),
            bindings,
            contextSnapshot,
            1000);
        producer.EnterQuarantine(
            ingress::ToMask(ingress::InputResetReason::QueueOverflow),
            contextSnapshot.contextRevision,
            bindings.controlMapRevision);

        const auto heldRepeat = producer.BuildIngressBatch(
            FakeObservedKbmBatch(
                2,
                { FakeKbmEvent(2, move, ingress::KbmEdgePhase::Press, false) },
                { move }),
            bindings,
            contextSnapshot,
            2000);
        Require(heldRepeat.completeCurrent.keyboardMoveHeldMask == 0 &&
                heldRepeat.physical.quarantineCodes.Contains(move),
            "explicit global recovery must quarantine raw held identity without rearming on repeat");

        const auto freshPress = producer.BuildIngressBatch(
            FakeObservedKbmBatch(
                3,
                { FakeKbmEvent(3, move, ingress::KbmEdgePhase::Press, true) },
                { move }),
            bindings,
            contextSnapshot,
            3000);
        Require(freshPress.completeCurrent.keyboardMoveHeldMask == 0x1 &&
                !freshPress.physical.quarantineCodes.Contains(move) &&
                freshPress.orderedEdges.size() == 1,
            "fresh initial press must prove a new physical epoch and rearm quarantined input");

        producer.RegisterSyntheticSuppression(ingress::SyntheticKeyboardSuppressionToken{
            .token = 99,
            .scancode = static_cast<std::uint8_t>(jump.idCode),
            .expectedPhase = ingress::KbmEdgePhase::Press,
            .contextRevision = contextSnapshot.contextRevision,
            .originatingOutputGeneration = 44,
            .helperInjectionSequence = 55,
            .provenanceMode = ingress::SyntheticProvenanceMode::ReservedNonCollidingControl,
            .remainingMatches = 1,
            .expiresAtOwnerUs = 10'000
        });
        producer.ResetSyntheticSuppression(
            ingress::ToMask(ingress::InputResetReason::SyntheticSuppressionReset));
        auto physicalJump = FakeKbmEvent(4, jump, ingress::KbmEdgePhase::Press);
        physicalJump.syntheticToken = 99;
        physicalJump.originatingOutputGeneration = 44;
        physicalJump.helperInjectionSequence = 55;
        const auto afterReset = producer.BuildIngressBatch(
            FakeObservedKbmBatch(4, { physicalJump }, { move, jump }),
            bindings,
            contextSnapshot,
            4000);
        Require(afterReset.orderedEdges.size() == 1,
            "global recovery must clear old synthetic tokens so later physical input is never swallowed");
    }

    void KbmOptionalRawReconcileRequiresCompletePhysicalProofFixture()
    {
        const auto bindings = FakeKbmBindingSnapshot(1, 5);
        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 7;
        const ingress::KbmPhysicalCode move{ ingress::KbmPhysicalDevice::Keyboard, 0x70 };

        ingress::KbmGameplayFactProducer disabled;
        (void)disabled.BuildIngressBatch(
            FakeObservedKbmBatch(1, { FakeKbmEvent(1, move, ingress::KbmEdgePhase::Press) }, { move }),
            bindings,
            contextSnapshot,
            1000);
        disabled.EnterQuarantine(
            ingress::ToMask(ingress::InputResetReason::FocusLost),
            contextSnapshot.contextRevision,
            bindings.controlMapRevision);
        auto unavailableRaw = FakeObservedKbmBatch(2, {}, {});
        unavailableRaw.rawCurrent.complete = false;
        unavailableRaw.rawCurrent.physicalOnlyProvenance = false;
        const auto quarantined = disabled.BuildIngressBatch(
            unavailableRaw,
            bindings,
            contextSnapshot,
            2000);
        Require(quarantined.orderedEdges.empty() &&
                quarantined.physical.quarantineCodes.Contains(move) &&
                quarantined.completeCurrent.keyboardMoveHeldMask == 0,
            "disabled or unproven raw provider must retain quarantine and never guess all-up");

        ingress::KbmGameplayFactProducer verified;
        (void)verified.BuildIngressBatch(
            FakeObservedKbmBatch(1, { FakeKbmEvent(1, move, ingress::KbmEdgePhase::Press) }, { move }),
            bindings,
            contextSnapshot,
            1000);
        const auto reconciled = verified.BuildIngressBatch(
            FakeObservedKbmBatch(2, {}, {}),
            bindings,
            contextSnapshot,
            2000);
        Require(reconciled.orderedEdges.size() == 1 &&
                reconciled.orderedEdges.front().phase == ingress::KbmEdgePhase::ReconciledRelease &&
                reconciled.orderedEdges.front().origin == ingress::KbmEdgeOrigin::Reconciled,
            "complete physical-only provider may emit exactly one evidence-backed reconciled release");
        const auto noDuplicate = verified.BuildIngressBatch(
            FakeObservedKbmBatch(3, {}, {}),
            bindings,
            contextSnapshot,
            3000);
        Require(noDuplicate.orderedEdges.empty(),
            "verified raw reconcile must consume the lost release exactly once");

        ingress::KbmGameplayFactProducer incomplete;
        (void)incomplete.BuildIngressBatch(
            FakeObservedKbmBatch(1, { FakeKbmEvent(1, move, ingress::KbmEdgePhase::Press) }, { move }),
            bindings,
            contextSnapshot,
            1000);
        auto incompleteRaw = FakeObservedKbmBatch(2, {}, {});
        incompleteRaw.rawCurrent.complete = false;
        const auto held = incomplete.BuildIngressBatch(
            incompleteRaw,
            bindings,
            contextSnapshot,
            2000);
        Require(held.orderedEdges.empty() && held.completeCurrent.keyboardMoveHeldMask == 0x1,
            "incomplete raw snapshot must not reconcile or erase a real held fact");
    }

    void KbmMappingBoundaryClearsOldSyntheticReceiptFixture()
    {
        ingress::KbmGameplayFactProducer producer;
        auto bindings = FakeKbmBindingSnapshot(1, 5);
        context::ResolvedContextSnapshot contextSnapshot{};
        contextSnapshot.contextRevision = 7;
        const ingress::KbmPhysicalCode jump{ ingress::KbmPhysicalDevice::Keyboard, 0x71 };
        (void)producer.BuildIngressBatch(
            FakeObservedKbmBatch(1, {}, {}),
            bindings,
            contextSnapshot,
            1000);
        producer.RegisterSyntheticSuppression(ingress::SyntheticKeyboardSuppressionToken{
            .token = 99,
            .scancode = static_cast<std::uint8_t>(jump.idCode),
            .expectedPhase = ingress::KbmEdgePhase::Press,
            .contextRevision = contextSnapshot.contextRevision,
            .originatingOutputGeneration = 44,
            .helperInjectionSequence = 55,
            .provenanceMode = ingress::SyntheticProvenanceMode::ReservedNonCollidingControl,
            .remainingMatches = 1,
            .expiresAtOwnerUs = 10'000
        });

        bindings.generation = 2;
        bindings.controlMapRevision = 6;
        std::reverse(bindings.entries.begin(), bindings.entries.end());
        auto physicalJump = FakeKbmEvent(1, jump, ingress::KbmEdgePhase::Press);
        physicalJump.syntheticToken = 99;
        physicalJump.originatingOutputGeneration = 44;
        physicalJump.helperInjectionSequence = 55;
        auto observed = FakeObservedKbmBatch(2, { physicalJump }, { jump });
        observed.rawCurrent.controlMapRevision = 6;
        const auto afterReload = producer.BuildIngressBatch(
            observed,
            bindings,
            contextSnapshot,
            2000);
        Require(afterReload.orderedEdges.size() == 1 &&
                afterReload.orderedEdges.front().origin == ingress::KbmEdgeOrigin::Physical,
            "mapping boundary must clear old synthetic receipt before evaluating new physical events");
    }

    void TestLatestPadStatePreventsSteadyAnalogQueueGrowth()
    {
        ingress::IngressHub hub{ 64 };
        auto baseline = LiveHidSnapshot(1, 0, 100);
        baseline.state.leftStick.x = 0.1f;
        Require(hub.PushPadSnapshot(baseline, false), "baseline HID transaction must publish");
        (void)hub.Capture(64);

        for (std::uint64_t sequence = 2; sequence <= 1001; ++sequence) {
            auto snapshot = LiveHidSnapshot(sequence, 0, sequence * 100);
            snapshot.state.leftStick.x = static_cast<float>(sequence) / 1001.0f;
            Require(hub.PushPadSnapshot(snapshot, false), "steady analog HID transaction must publish");
        }

        Require(hub.PendingCount() == 0, "steady analog reports must not grow the ordered edge queue");
        Require(hub.HasUncapturedLatest(), "steady analog reports must expose uncaptured latest work without inventing queue events");
        const auto capture = hub.Capture(64);
        Require(capture.events.empty(), "steady analog capture must not synthesize ordered events");
        Require(capture.latestPadState.has_value(), "steady analog capture must include latest pad state");
        Require(capture.latestPadState->generation == 1001, "latest pad state generation must advance per report");
        Require(capture.latestPadState->sourceSequence == 1001, "latest pad state must retain HID source sequence");
        Require(capture.latestPadState->state.leftStick.x == 1.0f, "latest pad state must retain newest axis value");
        Require(!hub.HasUncapturedLatest(), "capture must acknowledge the complete latest generation");
    }

    void TestLatestAnalogCanLeadBoundedDigitalEdgeCutoff()
    {
        ingress::IngressHub hub{ 16 };
        Require(hub.PushPadSnapshot(LiveHidSnapshot(1, 0, 100), false), "digital baseline must publish");
        (void)hub.Capture(16);

        auto press = LiveHidSnapshot(2, 0x1, 200);
        press.state.leftStick.x = 0.25f;
        auto release = LiveHidSnapshot(3, 0, 300);
        release.state.leftStick.x = 0.75f;
        Require(hub.PushPadSnapshot(press, false), "press transaction must publish");
        Require(hub.PushPadSnapshot(release, false), "release transaction must publish");

        const auto first = hub.Capture(1);
        Require(first.events.size() == 1, "one-event capture must stop at press cutoff");
        Require(first.remainingEvents == 1, "release must remain queued after press-only capture");
        Require(first.events[0].pad.samples.size() == 1 && first.events[0].pad.samples[0].pressed, "first ordered edge must be press");
        Require(first.latestPadState->state.leftStick.x == 0.75f, "latest analog may lead the edge cutoff");
        Require(first.latestPadState->currentDownMask == 0, "latest recovery mask may reflect a future release");

        const auto second = hub.Capture(1);
        Require(second.events.size() == 1 && second.events[0].pad.samples[0].released, "second ordered edge must be release");
        Require(second.remainingEvents == 0, "second capture must consume remaining release");
    }

    void TestFrameAssemblerDefersLatestAnalogBeyondOrderedEdgeCutoff()
    {
        ingress::IngressHub hub{ 16 };
        ingress::FrameAssembler assembler;
        Require(hub.PushPadSnapshot(LiveHidSnapshot(1, 0, 100), false), "assembler baseline must publish");
        auto capture = hub.Capture(16);
        (void)assembler.Assemble(capture);

        auto press = LiveHidSnapshot(2, 0x1, 200);
        press.state.leftStick.x = 0.25f;
        auto release = LiveHidSnapshot(3, 0, 300);
        release.state.leftStick.x = 0.75f;
        Require(hub.PushPadSnapshot(press, false), "assembler press must publish");
        Require(hub.PushPadSnapshot(release, false), "assembler release must publish");
        auto newestAnalog = LiveHidSnapshot(4, 0, 400);
        newestAnalog.state.leftStick.x = 0.9f;
        Require(hub.PushPadSnapshot(newestAnalog, false), "newest analog state must publish without another edge");

        capture = hub.Capture(1);
        auto frames = assembler.Assemble(capture);
        const auto& pressFrame = LastStableFrame(frames);
        const auto* pressSample = FindControlSample(pressFrame.facts, 0x1);
        const auto* axisSample = FindAxisSample(pressFrame.facts, input::PadAxisId::LeftStickX);
        Require(pressSample && pressSample->pressed && !pressSample->released, "first cutoff frame must contain only the ordered press edge");
        Require(axisSample && axisSample->scalar != 0.9f, "first cutoff frame must defer analog latest whose causal tail is still ahead");
        Require(FindPulse(pressFrame.facts, 0x1, false, true) == nullptr, "future release mask must not synthesize an early release");
        const auto pressMonotonicUs = pressFrame.facts.monotonicUs;

        capture = hub.Capture(1);
        frames = assembler.Assemble(capture);
        const auto& releaseFrame = LastStableFrame(frames);
        Require(FindPulse(releaseFrame.facts, 0x1, false, true) != nullptr, "second cutoff frame must deliver the queued release edge");
        axisSample = FindAxisSample(releaseFrame.facts, input::PadAxisId::LeftStickX);
        Require(axisSample && axisSample->scalar == 0.9f, "latest analog must apply once release reaches its causal tail");
        Require(releaseFrame.facts.monotonicUs >= pressMonotonicUs, "queued release must not regress stable fact time when latest becomes causally ready");
    }

    void TestOverflowRetainsLatestPadState()
    {
        ingress::IngressHub hub{ 1 };
        Require(hub.PushPadSnapshot(LiveHidSnapshot(1, 0, 100), false), "overflow baseline must publish");
        (void)hub.Capture(8);

        auto press = LiveHidSnapshot(2, 0x1, 200);
        press.state.rightStick.y = 0.25f;
        auto release = LiveHidSnapshot(3, 0, 300);
        release.state.rightStick.y = 0.9f;
        Require(hub.PushPadSnapshot(press, false), "first edge must fit queue");
        Require(!hub.PushPadSnapshot(release, false), "second edge must report overflow");

        const auto capture = hub.Capture(8);
        Require(capture.events.size() == 1 && capture.events[0].kind == ingress::IngressKind::QueueOverflow, "overflow must remain an ordered marker");
        Require(capture.latestPadState->state.rightStick.y == 0.9f, "overflow must not delete latest axis state");
        Require(capture.latestPadState->currentDownMask == 0, "overflow recovery baseline must retain current physical mask");
        Require(!capture.latestPadState->virtualGameplayEligible, "overflow-retained physical state must remain virtual-ineligible");
        Require(capture.latestPadState->inputStateEpoch == capture.inputStateEpoch, "overflow latest and capture must share the new global epoch");
    }

    void TestOverflowFreezesDigitalEdgesUntilCleanRelease()
    {
        ingress::IngressHub hub{ 1 };
        Require(hub.PushEvent(Manifest(1)), "overflow freeze fixture must fill the queue");
        Require(
            !hub.PushPadSnapshot(LiveHidSnapshot(1, 0x1, 100), false),
            "press arriving into a full queue must establish edge-history-lost recovery");
        auto capture = hub.Capture(8);
        Require(capture.events.size() == 1 && capture.events[0].kind == ingress::IngressKind::QueueOverflow, "overflow freeze must publish one recovery marker");

        Require(
            hub.PushPadSnapshot(LiveHidSnapshot(2, 0x3, 200), false),
            "additional press while recovery is active must update latest state without failing publication");
        Require(hub.PendingCount() == 0, "additional press must remain frozen while any physical button is down");

        Require(
            hub.PushPadSnapshot(LiveHidSnapshot(3, 0, 300), false),
            "clean release must establish the new recovery baseline");
        Require(hub.PendingCount() == 0, "clean release must not leak stale release pulses after overflow");

        Require(
            hub.PushPadSnapshot(LiveHidSnapshot(4, 0x1, 400), false),
            "first press after clean release must resume ordered edge delivery");
        capture = hub.Capture(8);
        Require(capture.events.size() == 1, "post-recovery press must enqueue exactly one edge event");
        Require(capture.events[0].pad.samples.size() == 1 && capture.events[0].pad.samples[0].pressed, "post-recovery event must be the new press");
    }

    void TestSourceEvidenceUsesLatestPublicationWithoutQueueGrowth()
    {
        ingress::IngressHub hub{ 16 };
        hub.PublishSourceEvidenceFrame(GamepadSourceFrame(1, 1, true));
        const auto initial = hub.Capture(16);
        Require(initial.events.size() == 1 && initial.events[0].kind == ingress::IngressKind::DeviceFamilyChanged, "source change must retain ordered device boundary");

        for (std::uint64_t tick = 2; tick <= 1001; ++tick) {
            hub.PublishSourceEvidenceFrame(GamepadSourceFrame(1, tick, false));
        }
        Require(hub.PendingCount() == 0, "repeated source snapshots must not grow ordered queue");
        const auto capture = hub.Capture(16);
        Require(capture.latestSourceEvidence.has_value(), "capture must include latest source evidence");
        Require(capture.latestSourceEvidence->generation == 1001, "source publication generation must advance");
        Require(capture.latestSourceEvidence->snapshot.collectedTick == 1001, "latest source evidence must retain newest tick");
    }

    void TestConcurrentCaptureNeverObservesHalfHidTransaction()
    {
        ingress::IngressHub hub{ 64 };
        std::atomic_bool writerDone{ false };
        std::atomic_bool writerFailed{ false };
        std::thread writer([&]() {
            for (std::uint64_t sequence = 1; sequence <= 10'000; ++sequence) {
                auto snapshot = LiveHidSnapshot(sequence, 0, sequence);
                snapshot.state.leftStick.x = static_cast<float>(sequence % 100) / 100.0f;
                const auto source = GamepadSourceFrame(1, sequence, sequence == 1);
                if (!hub.PushPadSnapshot(snapshot, false, &source)) {
                    writerFailed.store(true, std::memory_order_release);
                    break;
                }
            }
            writerDone.store(true, std::memory_order_release);
        });

        std::size_t coherentCaptures = 0;
        while (!writerDone.load(std::memory_order_acquire)) {
            const auto capture = hub.Capture(64);
            if (capture.latestPadState && capture.latestSourceEvidence) {
                Require(
                    capture.latestPadState->sourceSequence == capture.latestSourceEvidence->snapshot.collectedTick,
                    "capture must not observe source evidence and pad state from different HID transactions");
                ++coherentCaptures;
            }
            std::this_thread::yield();
        }
        writer.join();

        const auto finalCapture = hub.Capture(64);
        Require(!writerFailed.load(std::memory_order_acquire), "coherent HID writer must not overflow on state-only reports");
        Require(finalCapture.latestPadState && finalCapture.latestSourceEvidence, "final coherent capture must include both publications");
        Require(
            finalCapture.latestPadState->sourceSequence == finalCapture.latestSourceEvidence->snapshot.collectedTick,
            "final coherent capture must come from one complete HID transaction");
        Require(finalCapture.latestPadState->sourceSequence == 10'000, "final coherent capture must retain the last transaction");
        Require(coherentCaptures != 0 || finalCapture.latestPadState.has_value(), "coherent capture fixture must observe a publication");
    }

    void TestLatestSourceEvidenceCannotBypassQueuedDeviceBoundary()
    {
        ingress::IngressHub hub{ 16 };
        ingress::FrameAssembler assembler;
        Require(hub.PushEvent(PadSample(0x1, true, true, false)), "source cutoff fixture must enqueue an older edge");
        hub.PublishSourceEvidenceFrame(GamepadSourceFrame(1, 200, true));

        auto capture = hub.Capture(1);
        auto frames = assembler.Assemble(capture.events, capture.latestPadState, capture.latestSourceEvidence);
        const auto& beforeBoundary = LastStableFrame(frames);
        Require(beforeBoundary.boundaryKey.deviceFamilyRevision == 0, "latest source must not advance beyond an undrained device marker");
        Require(beforeBoundary.facts.latestSourceEvidenceGeneration == 0, "deferred source generation must remain unapplied");
        Require(capture.remainingEvents == 1, "device marker must remain queued behind the older edge");

        capture = hub.Capture(1);
        frames = assembler.Assemble(capture.events, capture.latestPadState, capture.latestSourceEvidence);
        const auto& afterBoundary = LastStableFrame(frames);
        Require(afterBoundary.boundaryKey.deviceFamilyRevision == 1, "source evidence must publish after its marker reaches the cutoff");
        Require(afterBoundary.facts.latestSourceEvidenceGeneration == 1, "paired latest source generation must publish once ordered marker is consumed");
    }

    void TestLatestSourceEvidenceAheadOfCapturedBoundaryWaitsForMatchingMarker()
    {
        ingress::IngressHub hub{ 16 };
        ingress::FrameAssembler assembler;
        ingress::LatestPadState latestPad{
            .generation = 1,
            .sourceSequence = 1,
            .sourceTimestampUs = 350,
            .contextEpoch = 0,
            .contextRevision = 0
        };
        latestPad.state.leftStick.x = 0.5f;
        hub.PublishSourceEvidenceFrame(GamepadSourceFrame(1, 100, true));
        hub.PublishSourceEvidenceFrame(KeyboardMouseSourceFrame(2, 200, true));
        hub.PublishSourceEvidenceFrame(GamepadSourceFrame(3, 300, true));

        for (std::uint32_t revision = 1; revision <= 3; ++revision) {
            const auto capture = hub.Capture(1);
            Require(capture.events.size() == 1, "one queued family boundary must be captured per owner tick");
            const auto frames = assembler.Assemble(
                capture.events,
                latestPad,
                capture.latestSourceEvidence);
            Require(
                FindTransition(frames, ingress::TransitionReason::ExplicitReset) == nullptr,
                "a latest-wins source snapshot ahead of the capture cutoff must be deferred, not treated as mismatch");
            Require(
                capture.remainingEvents == 3 - revision,
                "newer family boundaries must remain ordered behind the capture cutoff");

            const auto stableIt = std::find_if(
                frames.begin(),
                frames.end(),
                [](const ingress::AssembledFactFrame& frame) {
                    return frame.kind == ingress::AssembledFrameKind::Stable;
                });
            if (revision < 3) {
                Require(
                    stableIt == frames.end(),
                    "latest pad state must not bypass an unpaired device-family boundary");
            } else {
                Require(stableIt != frames.end(), "matching marker must release the deferred stable frame");
                const auto& stable = LastStableFrame(frames);
                Require(
                    stable.boundaryKey.deviceFamilyRevision == 3,
                    "the latest source snapshot must publish after its matching boundary is consumed");
                Require(
                    stable.facts.latestSourceEvidenceGeneration == 3,
                    "deferred latest source generation must publish exactly at its matching cutoff");
                Require(
                    stable.facts.latestPadStateGeneration == 1,
                    "latest pad generation must publish after the device-family pair is complete");
            }
        }
    }

    void TestOrderedPadEventWaitsForDeviceSourcePair()
    {
        ingress::FrameAssembler assembler;
        ingress::LatestSourceEvidence futureSource{
            .generation = 2,
            .snapshot = presentation::SourceEvidenceSnapshot{
                .deviceFamilyEvidence = presentation::PublishedDeviceFamilyEvidence{
                    .family = presentation::DeviceFamily::KeyboardMouse,
                    .deviceFamilyRevision = 2,
                    .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
                    .publishedTick = 200
                },
                .keyboardEvidence = true,
                .collectedTick = 200
            }
        };
        const auto frames = assembler.Assemble(
            AssignSeq({
                DeviceMarker(presentation::DeviceFamily::Gamepad, 1),
                PadSample(0x1, true, true, false)
            }),
            std::nullopt,
            futureSource);

        Require(
            std::none_of(
                frames.begin(),
                frames.end(),
                [](const ingress::AssembledFactFrame& frame) {
                    return frame.kind == ingress::AssembledFrameKind::Stable;
                }),
            "ordered pad facts must not publish while the device-family marker is unpaired");
    }

    void TestLatestAnalogCannotBypassQueuedContextBoundary()
    {
        ingress::IngressHub hub{ 16 };
        ingress::FrameAssembler assembler;
        Require(hub.PushEvent(PadSample(0x1, true, true, false)), "context cutoff fixture must enqueue an older edge");
        auto latest = LiveHidSnapshot(1, 0, 200);
        latest.contextEpoch = 2;
        latest.contextRevision = 3;
        latest.state.leftStick.x = 0.9f;
        Require(hub.PushPadSnapshot(latest, false), "new-context latest state must publish");

        auto capture = hub.Capture(1);
        auto frames = assembler.Assemble(capture.events, capture.latestPadState, capture.latestSourceEvidence);
        const auto& beforeBoundary = LastStableFrame(frames);
        Require(beforeBoundary.boundaryKey.contextRevision == 0, "older edge must remain in the old context cutoff");
        Require(FindAxisSample(beforeBoundary.facts, input::PadAxisId::LeftStickX) == nullptr, "new-context analog must wait for its queued UI boundary");
        Require(beforeBoundary.facts.latestPadStateGeneration == 0, "deferred latest pad generation must remain unapplied");

        capture = hub.Capture(1);
        frames = assembler.Assemble(capture.events, capture.latestPadState, capture.latestSourceEvidence);
        const auto& afterBoundary = LastStableFrame(frames);
        const auto* axis = FindAxisSample(afterBoundary.facts, input::PadAxisId::LeftStickX);
        Require(afterBoundary.boundaryKey.contextRevision == 3 && afterBoundary.boundaryKey.menuStackRevision == 2, "queued UI boundary must advance before latest analog");
        Require(axis && axis->scalar == 0.9f, "latest analog must publish after its context boundary reaches the cutoff");
        Require(afterBoundary.facts.latestPadStateGeneration == 1, "paired latest pad generation must publish after the UI boundary");
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

    void TestLegacySnapshotAdapterPrefersInputV2ContextRevision()
    {
        input::PadEventSnapshot snapshot{};
        snapshot.sequence = 20;
        snapshot.firstSequence = 20;
        snapshot.sourceTimestampUs = 4321;
        snapshot.contextEpoch = 1;
        snapshot.contextRevision = 23;
        snapshot.state.timestampUs = 4321;
        snapshot.state.sequence = 20;

        const auto converted = ingress::ConvertLegacySnapshotToIngressEvents(snapshot, 19);
        Require(converted.size() == 2, "snapshot with continuous sequence must produce ui + pad ingress events");
        Require(converted[0].kind == ingress::IngressKind::UiSnapshot, "first converted event must be UiSnapshot");
        Require(
            converted[0].ui.contextRevision == 23,
            "legacy snapshot adapter must prefer input_v2 contextRevision over legacy contextEpoch");
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
        Require(converted[0].sequenceGap.expected == 11, "SequenceGap must capture expected device report seq");
        Require(converted[0].sequenceGap.actual == 12, "SequenceGap must capture actual device report seq");
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

    void TestCoalescedHeldHidPressSampleTriggersInteractionEngine()
    {
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& hub = ingress::IngressHub::GetSingleton();
        (void)hub.PushEvent(Manifest(42));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(10, 0x0, 10'000));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(11, 0x1, 11'000));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(12, 0x1, 12'000));

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(hub.Drain());
        const auto& stable = LastFrame(frames);
        const auto* sample = FindControlSample(stable.facts, 0x1);
        Require(sample != nullptr, "coalesced held HID press must retain a latest control sample");
        Require(sample->down, "coalesced held HID press must keep the button down");
        Require(sample->pressed, "coalesced held HID press must retain the press edge");
        Require(sample->downAtUs == 11'000, "coalesced held HID press must retain original downAtUs");
        Require(sample->timestampUs == 12'000, "coalesced held HID press must keep the latest sample timestamp");
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
        Require(resolved.changes.size() == 1, "coalesced held HID press must trigger an action phase");
        Require(resolved.changes[0].actionId == "Jump", "coalesced held HID press must resolve the bound action");
        Require(resolved.changes[0].phase == actions::ActionPhase::Press, "coalesced held HID press must emit Press");
    }

    void TestPublishedPressEdgeDoesNotCarryIntoNextStableFrame()
    {
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& hub = ingress::IngressHub::GetSingleton();
        (void)hub.PushEvent(Manifest(42));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(20, 0x0, 20'000));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(21, 0x1, 21'000));

        ingress::FrameAssembler assembler;
        auto frames = assembler.Assemble(hub.Drain());
        const auto& pressStable = LastFrame(frames);
        const auto* pressSample = FindControlSample(pressStable.facts, 0x1);
        Require(pressSample != nullptr, "first stable frame must carry the pressed control sample");
        Require(pressSample->pressed, "first stable frame must publish the press edge");
        Require(FindPulse(pressStable.facts, 0x1, true, false) != nullptr, "first stable frame must publish the press pulse");

        (void)hub.PushPadSnapshot(LiveHidSnapshot(22, 0x1, 22'000));
        frames = assembler.Assemble(hub.Drain());
        const auto& heldStable = LastFrame(frames);
        const auto* heldSample = FindControlSample(heldStable.facts, 0x1);
        Require(heldSample != nullptr, "held stable frame must carry the held control sample");
        Require(heldSample->down, "held stable frame must preserve button down state");
        Require(!heldSample->pressed, "published press edge must not carry into the next stable frame");
        Require(!heldSample->released, "held stable frame must not synthesize release");
        Require(heldStable.facts.pulseLedger.empty(), "published press pulse must not carry into the next stable frame");
    }

    void TestPublishedReleaseEdgeDoesNotCarryIntoNextStableFrame()
    {
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& hub = ingress::IngressHub::GetSingleton();
        (void)hub.PushEvent(Manifest(42));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(30, 0x0, 30'000));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(31, 0x1, 31'000));

        ingress::FrameAssembler assembler;
        (void)assembler.Assemble(hub.Drain());

        (void)hub.PushPadSnapshot(LiveHidSnapshot(32, 0x0, 32'000));
        auto frames = assembler.Assemble(hub.Drain());
        const auto& releaseStable = LastFrame(frames);
        const auto* releaseSample = FindControlSample(releaseStable.facts, 0x1);
        Require(releaseSample != nullptr, "release stable frame must carry the released control sample");
        Require(!releaseSample->down, "release stable frame must mark the button up");
        Require(!releaseSample->pressed, "release stable frame must not keep the prior press edge");
        Require(releaseSample->released, "release stable frame must publish the release edge");
        Require(FindPulse(releaseStable.facts, 0x1, false, true) != nullptr, "release stable frame must publish the release pulse");

        (void)hub.PushPadSnapshot(LiveHidSnapshot(33, 0x0, 33'000));
        frames = assembler.Assemble(hub.Drain());
        const auto& idleStable = LastFrame(frames);
        const auto* idleSample = FindControlSample(idleStable.facts, 0x1);
        Require(idleSample != nullptr, "idle stable frame may retain the latest up control sample");
        Require(!idleSample->down, "idle stable frame must keep the button up");
        Require(!idleSample->pressed, "published press edge must not carry into idle");
        Require(!idleSample->released, "published release edge must not carry into idle");
        Require(idleStable.facts.pulseLedger.empty(), "published release pulse must not carry into idle");
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

        const auto capture = ingress::IngressHub::GetSingleton().Capture(16);
        Require(capture.events.size() == 1, "device family seam must enqueue only the ordered marker");
        Require(capture.events[0].kind == ingress::IngressKind::DeviceFamilyChanged, "device marker must be first");
        Require(capture.latestSourceEvidence.has_value(), "source evidence must publish through the latest slot");
        Require(
            capture.events[0].deviceFamily.deviceFamilyRevision ==
                capture.latestSourceEvidence->snapshot.deviceFamilyEvidence.deviceFamilyRevision,
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

        const auto capture = ingress::IngressHub::GetSingleton().Capture(16);
        Require(capture.events.size() == 1, "live gamepad evidence must enqueue only the ordered marker");
        Require(capture.events[0].kind == ingress::IngressKind::DeviceFamilyChanged, "live gamepad evidence marker must be first");
        Require(capture.latestSourceEvidence.has_value(), "live gamepad source evidence must publish through the latest slot");
        Require(
            capture.events[0].deviceFamily.deviceFamilyRevision ==
                capture.latestSourceEvidence->snapshot.deviceFamilyEvidence.deviceFamilyRevision,
            "live SourceEvidence must mirror the marker deviceFamilyRevision");
        Require(
            capture.latestSourceEvidence->snapshot.deviceFamilyEvidence.family == presentation::DeviceFamily::Gamepad,
            "live SourceEvidence must publish gamepad family");
        Require(capture.latestSourceEvidence->snapshot.gamepadEvidence, "live SourceEvidence must record gamepad evidence");
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
        auto capture = ingress::IngressHub::GetSingleton().Capture(16);
        Require(capture.events.size() == 1, "gamepad evidence must publish one ordered marker");
        Require(capture.latestSourceEvidence->snapshot.gamepadEvidence, "gamepad evidence must set gamepadEvidence");
        Require(capture.latestSourceEvidence->snapshot.gamepadLease, "gamepad evidence must establish gamepad lease");

        producer.PublishKeyboardSourceEvidence(contextSnapshot, 0x1E, 101'000);
        capture = ingress::IngressHub::GetSingleton().Capture(16);
        Require(capture.events.size() == 1, "keyboard evidence must publish one takeover marker");
        Require(
            capture.events[0].deviceFamily.family == presentation::DeviceFamily::KeyboardMouse,
            "keyboard evidence must publish KeyboardMouse marker");
        Require(capture.latestSourceEvidence->snapshot.keyboardEvidence, "keyboard evidence must set keyboardEvidence");
        Require(!capture.latestSourceEvidence->snapshot.gamepadEvidence, "keyboard evidence must clear gamepad evidence");
        Require(!capture.latestSourceEvidence->snapshot.gamepadLease, "keyboard evidence must clear gamepad lease");

        producer.PublishGamepadSourceEvidence(contextSnapshot, 102'000);
        capture = ingress::IngressHub::GetSingleton().Capture(16);
        Require(capture.events.size() == 1, "gamepad reclaim must publish one ordered marker");
        Require(
            capture.events[0].deviceFamily.family == presentation::DeviceFamily::Gamepad,
            "gamepad reclaim must publish Gamepad marker");
        Require(capture.latestSourceEvidence->snapshot.gamepadEvidence, "gamepad reclaim must restore gamepadEvidence");
        Require(!capture.latestSourceEvidence->snapshot.keyboardEvidence, "gamepad reclaim must clear keyboardEvidence");

        producer.PublishMouseMoveSourceEvidence(contextSnapshot, 5, -3, 103'000);
        capture = ingress::IngressHub::GetSingleton().Capture(16);
        Require(capture.events.size() == 1, "mouse move evidence must publish one takeover marker");
        Require(capture.latestSourceEvidence->snapshot.mouseMoveEvidence, "mouse move evidence must set mouseMoveEvidence");
        Require(
            capture.latestSourceEvidence->snapshot.pointerSignal == presentation::PointerSignal::HoverOnly,
            "mouse move evidence must publish hover pointer signal");

        producer.PublishGamepadSourceEvidence(contextSnapshot, 104'000);
        (void)ingress::IngressHub::GetSingleton().Capture(16);
        producer.PublishMouseButtonSourceEvidence(contextSnapshot, 105'000);
        capture = ingress::IngressHub::GetSingleton().Capture(16);
        Require(capture.events.size() == 1, "mouse button evidence must publish one takeover marker");
        Require(capture.latestSourceEvidence->snapshot.mouseButtonEvidence, "mouse button evidence must set mouseButtonEvidence");
        Require(
            capture.latestSourceEvidence->snapshot.pointerSignal == presentation::PointerSignal::PointerActive,
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
        (void)ingress::IngressHub::GetSingleton().Capture(16);

        producer.MarkSyntheticKeyboardScancode(0x64, 1, 250'000, 201'000);
        producer.PublishKeyboardSourceEvidence(contextSnapshot, 0x64, 201'100);

        const auto capture = ingress::IngressHub::GetSingleton().Capture(16);
        Require(capture.events.empty(), "synthetic keyboard evidence must not publish an ordered takeover event");
        Require(capture.latestSourceEvidence.has_value(), "synthetic keyboard evidence must refresh the latest source slot");
        Require(
            capture.latestSourceEvidence->snapshot.deviceFamilyEvidence.family == presentation::DeviceFamily::Gamepad,
            "synthetic keyboard evidence must keep the current Gamepad family");
        Require(!capture.latestSourceEvidence->snapshot.keyboardEvidence, "synthetic keyboard evidence must not set keyboardEvidence");
        Require(capture.latestSourceEvidence->snapshot.syntheticKeyboardWindow, "synthetic keyboard evidence must mark the synthetic window");
        Require(capture.latestSourceEvidence->snapshot.gamepadLease, "synthetic keyboard evidence must not clear the gamepad lease");
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
        Require(gap == nullptr, "device report SequenceGap marker must stay diagnostic and not dispatch recovery transition");
        Require(overflow != nullptr, "queue overflow transition required");
        Require(overflow->transition.requestHardResync, "queue overflow must hard reset");
        Require(ToGameplayRecoveryInput(*overflow).hardResetRequested, "queue overflow maps to hard recovery input");
    }

    void TestDeviceReportSequenceGapIsSoftDiagnosticAndKeepsDigitalEdge()
    {
        ingress::LiveInputFactProducer::GetSingleton().ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& hub = ingress::IngressHub::GetSingleton();
        (void)hub.PushEvent(Manifest(42));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(1, 0x0, 1'000));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(3, 0x1, 3'000));

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(hub.Drain());
        Require(
            FindTransition(frames, ingress::TransitionReason::SequenceGap) == nullptr,
            "SequenceGapWithoutDroppedDigitalEdges_IsSoftGap");
        const auto& stable = LastStableFrame(frames);
        Require(!stable.facts.health.sequenceGap, "device report gap must not poison stable frame recovery health");
        const auto* press = FindPulse(stable.facts, 0x1, true, false);
        Require(press != nullptr, "device report gap must preserve digital press edge");
        Require(press->timestampUs == 3'000, "preserved digital edge must keep the latest HID timestamp");
    }

    void TestCoalescedHidReportsDoNotHardResetOutputs()
    {
        ingress::LiveInputFactProducer::GetSingleton().ResetForTests();

        input::PadEventSnapshot snapshot = LiveHidSnapshot(8, 0x1, 8'000);
        snapshot.firstSequence = 6;
        snapshot.coalesced = true;

        const auto converted = ingress::ConvertLegacySnapshotToIngressEvents(snapshot, 5);
        for (const auto& event : converted) {
            Require(
                event.kind != ingress::IngressKind::ExplicitReset,
                "CoalescedHidReports_DoNotHardResetOutputs");
        }

        auto events = converted;
        events.insert(events.begin(), Manifest(42));

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(AssignSeq(std::move(events)));
        Require(
            FindTransition(frames, ingress::TransitionReason::ExplicitReset) == nullptr,
            "coalesced HID reports must not emit explicit reset");
        Require(
            FindTransition(frames, ingress::TransitionReason::QueueOverflow) == nullptr,
            "coalesced HID reports must not emit hard overflow recovery");
        const auto& stable = LastStableFrame(frames);
        Require(stable.facts.health.coalescedSnapshot, "coalesced stable frame must retain diagnostic health bit");
        Require(FindPulse(stable.facts, 0x1, true, false) != nullptr, "CoalescedGap must preserve digital edge");
    }

    void TestRuntimeSnapshotSeqGapWithoutBoundaryChangeIsSoftTransition()
    {
        auto events = AssignSeq({
            Manifest(42),
            PadSample(0x1, true, true, false),
            PadSample(0x1, false, false, true)
        });
        events[2].seq = events[1].seq + 2;

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(events);
        const auto* gap = FindTransition(frames, ingress::TransitionReason::SequenceGap);
        Require(gap != nullptr, "runtime snapshot seq gap must remain visible as a runtime transition");
        Require(gap->transition.requestSoftResync, "runtime snapshot seq gap must soft resync");
        Require(!gap->transition.requestHardResync, "runtime snapshot seq gap must not hard reset outputs");
        Require(!gap->transition.flushPendingPulseEdges, "SoftGap must not flush pending pulse edges");

        const auto recovery = ToGameplayRecoveryInput(*gap);
        Require(recovery.sequenceGapObserved, "runtime snapshot seq gap must map to recovery input");
        Require(recovery.softResyncRequested, "runtime snapshot seq gap recovery input must be soft");
        Require(!recovery.hardResetRequested, "runtime snapshot seq gap recovery input must not be hard");
    }

    void TestAxisOnlyCoalescingDoesNotClearHeldButton()
    {
        ingress::LiveInputFactProducer::GetSingleton().ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto held = LiveHidSnapshot(1, 0x1, 1'000);
        auto axisOnly = LiveHidSnapshot(2, 0x1, 2'000);
        axisOnly.coalesced = true;
        axisOnly.state.leftStick.x = 0.5f;

        auto& hub = ingress::IngressHub::GetSingleton();
        (void)hub.PushEvent(Manifest(42));
        (void)hub.PushPadSnapshot(held);
        (void)hub.PushPadSnapshot(axisOnly);

        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(hub.Drain());
        Require(
            FindTransition(frames, ingress::TransitionReason::ExplicitReset) == nullptr,
            "AxisOnlyCoalescing_DoesNotClearHeldButton must not hard reset");
        const auto& stable = LastStableFrame(frames);
        const auto* heldSample = FindControlSample(stable.facts, 0x1);
        Require(heldSample != nullptr, "axis-only coalescing must retain held digital sample");
        Require(heldSample->down, "axis-only coalescing must not clear held digital down state");
        Require(!heldSample->released, "axis-only coalescing must not synthesize release");
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

    void TestFrameAssemblerUsesIngressSeqForMonotonicTimeRegression()
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
            FindTransition(frames, ingress::TransitionReason::SequenceGap) == nullptr,
            "a producer timestamp regression with contiguous ingress seq is not sequence loss");

        const auto& stable = LastStableFrame(frames);
        Require(
            FindPulse(stable.facts, 1, false, true) != nullptr,
            "seq-ordered release must remain visible even when its capture timestamp is older");
        Require(
            stable.facts.monotonicUs == events[1].monotonicUs,
            "frame evaluation time must retain the maximum observed capture timestamp");
    }

    void TestDeviceFamilyEvidenceFromIndependentThreadsUsesIngressSeqAuthority()
    {
        ingress::IngressHub hub{ 16 };
        ingress::FrameAssembler assembler;

        hub.PublishSourceEvidenceFrame(GamepadSourceFrame(1, 2'000'000, true));
        hub.PublishSourceEvidenceFrame(KeyboardMouseSourceFrame(2, 1'999'000, true));
        hub.PublishSourceEvidenceFrame(GamepadSourceFrame(3, 2'001'000, true));

        const auto capture = hub.Capture(16);
        Require(capture.events.size() == 3, "three family boundary markers must be ordered by ingress seq");
        const auto frames = assembler.Assemble(
            capture.events,
            capture.latestPadState,
            capture.latestSourceEvidence);

        Require(
            FindTransition(frames, ingress::TransitionReason::SequenceGap) == nullptr,
            "independent HID and keyboard capture clocks must not manufacture sequence loss");
        Require(
            FindTransition(frames, ingress::TransitionReason::ExplicitReset) == nullptr,
            "clock overlap must not hard-reset a correctly paired latest source snapshot");
        const auto& stable = LastStableFrame(frames);
        Require(
            stable.boundaryKey.deviceFamilyRevision == 3,
            "all seq-ordered device-family boundaries must be consumed");
        Require(
            stable.facts.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision == 3,
            "the latest source snapshot must pair with the last consumed boundary");
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
        Require(stable.facts.overflowCompaction.has_value(), "overflow baseline must expose typed compaction debug summary");
        Require(
            stable.facts.overflowCompaction->transitionObserved,
            "overflow compaction debug summary must expose the transition");
        Require(
            stable.facts.overflowCompaction->typedCompactionApplied,
            "overflow compaction debug summary must mark typed compaction");
        Require(
            stable.facts.overflowCompaction->retainedManifest &&
                stable.facts.overflowCompaction->retainedUi &&
                stable.facts.overflowCompaction->retainedDeviceFamily &&
                stable.facts.overflowCompaction->retainedSourceEvidence,
            "overflow compaction debug summary must expose retained boundary facts");
        Require(
            stable.facts.overflowCompaction->droppedControlSamples,
            "overflow compaction debug summary must expose dropped volatile controls");
        Require(
            stable.facts.overflowCompaction->debugSummary.find("retained_manifest=true") != std::string::npos &&
                stable.facts.overflowCompaction->debugSummary.find("dropped_control_samples=true") != std::string::npos,
            "overflow compaction debug summary must be loggable text");
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

    void TestStaleSourceEvidenceAfterNewerDeviceMarkerDoesNotHardReset()
    {
        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(AssignSeq({
            Manifest(1),
            Ui(1, 1),
            DeviceMarker(presentation::DeviceFamily::KeyboardMouse, 1),
            SourceEvidence(1),
            DeviceMarker(presentation::DeviceFamily::KeyboardMouse, 2),
            DeviceMarker(presentation::DeviceFamily::Gamepad, 3),
            SourceEvidence(2),
            SourceEvidence(3),
            PadSample(7, true, true, false)
        }));

        Require(
            FindTransition(frames, ingress::TransitionReason::ExplicitReset) == nullptr,
            "stale older source evidence must be ignored instead of forcing a hard reset");
        const auto& stable = LastStableFrame(frames);
        Require(
            stable.boundaryKey.deviceFamilyRevision == 3,
            "newer paired device marker must remain the active boundary revision");
        Require(
            stable.facts.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision == 3,
            "newer source evidence must publish after stale evidence is ignored");
        Require(
            !stable.facts.health.boundaryMarkerMismatch,
            "ignored stale source evidence must not poison stable frame health");
    }

    void TestMissingDeviceMarkerSourceEvidenceSoftSyncsBoundary()
    {
        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble(AssignSeq({
            Manifest(1),
            Ui(1, 1),
            SourceEvidence(5),
            PadSample(7, true, true, false)
        }));

        Require(
            FindTransition(frames, ingress::TransitionReason::ExplicitReset) == nullptr,
            "missing device marker source evidence must not hard reset repeatedly");
        const auto* boundary = FindTransition(frames, ingress::TransitionReason::BoundaryKeyChanged);
        Require(boundary != nullptr, "missing device marker source evidence must soft-sync the boundary");
        Require(!boundary->transition.requestHardResync, "missing device marker source evidence must not hard reset outputs");
        const auto& stable = LastStableFrame(frames);
        Require(
            stable.boundaryKey.deviceFamilyRevision == 5,
            "source evidence ahead of a missing marker must advance device boundary revision");
        Require(
            stable.facts.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision == 5,
            "soft-synced source evidence must publish to the stable fact frame");
        Require(
            !stable.facts.health.boundaryMarkerMismatch,
            "soft-synced source evidence must not poison stable frame health");
        Require(FindControlSample(stable.facts, 7) != nullptr, "soft-sync must keep following control samples");
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

    void TestLegacySnapshotCannotOverrideKernelFacts()
    {
        ingress::AssembledFactFrame stable{};
        stable.kind = ingress::AssembledFrameKind::Stable;
        stable.lastSeq = 77;
        stable.boundaryKey = ingress::IngressBoundaryKey{ 42, 101, 102, 103 };
        stable.facts.manifestEpoch = 999;
        stable.facts.contextRevision = 888;
        stable.facts.menuStackRevision = 887;
        stable.facts.deviceFamilyRevision = 886;
        stable.facts.monotonicUs = 55'000;
        stable.facts.controlSamples.push_back(actions::ControlSample{
            .path = actions::ControlPath{
                .kind = actions::ControlPathKind::DigitalButton,
                .code = 9
            },
            .down = true,
            .pressed = true,
            .timestampUs = 54'000
        });

        input::PadEventSnapshot legacy{};
        legacy.sequence = 1234;
        legacy.contextEpoch = 777;
        legacy.sourceTimestampUs = 1;
        legacy.state.buttons.digitalMask = 0;
        stable.facts.legacySnapshot = legacy;

        const auto kernel = ingress::BuildKernelFrame(stable);
        Require(kernel.facts.manifestEpoch == 42, "kernel manifest epoch must come from ingress boundary key");
        Require(kernel.facts.contextRevision == 101, "kernel context revision must come from ingress boundary key");
        Require(kernel.facts.menuStackRevision == 102, "kernel menu stack revision must come from ingress boundary key");
        Require(kernel.facts.deviceFamilyRevision == 103, "kernel device family revision must come from ingress boundary key");
        Require(kernel.facts.monotonicUs == 55'000, "kernel monotonic time comes from input_v2 fact frame");
        Require(kernel.kernelRevision == 77, "kernel revision comes from assembled frame sequence");
        Require(kernel.state.controlSamples.size() == 1, "kernel only carries input_v2 control samples");
        Require(kernel.state.controlSamples[0].path.code == 9, "kernel sample must not be rebuilt from legacy snapshot");
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

    void TestDeterministicProducerOwnerRateMatrix()
    {
        const auto percentile = [](std::vector<std::uint64_t> values, std::size_t numerator) {
            Require(!values.empty(), "rate-matrix latency samples must not be empty");
            std::sort(values.begin(), values.end());
            const auto index = std::min(values.size() - 1, (values.size() * numerator + 99) / 100 - 1);
            return values[index];
        };

        for (const auto producerHz : { 500u, 1000u }) {
            for (const auto ownerHz : { 30u, 60u, 120u }) {
                for (const auto producerWinsTie : { false, true }) {
                    ingress::IngressHub hub{ 16 };
                    auto baseline = LiveHidSnapshot(1, 0, 0);
                    baseline.state.leftStick.x = 0.0f;
                    Require(hub.PushPadSnapshot(baseline, false), "rate-matrix baseline must publish");
                    (void)hub.Capture(64);
                    std::uint32_t producerIndex = 1;
                    std::uint32_t ownerIndex = 0;
                    std::uint64_t lastObservedGeneration = 0;
                    std::vector<std::uint64_t> semanticLatencyUs;

                    while (producerIndex < producerHz || ownerIndex < ownerHz) {
                        const auto producerTimeUs = producerIndex < producerHz ?
                            (static_cast<std::uint64_t>(producerIndex) * 1'000'000ull) / producerHz :
                            std::numeric_limits<std::uint64_t>::max();
                        const auto ownerTimeUs = ownerIndex < ownerHz ?
                            (static_cast<std::uint64_t>(ownerIndex) * 1'000'000ull) / ownerHz :
                            std::numeric_limits<std::uint64_t>::max();
                        const bool publish = producerTimeUs < ownerTimeUs ||
                            (producerTimeUs == ownerTimeUs && producerWinsTie);
                        if (publish) {
                            auto snapshot = LiveHidSnapshot(
                                static_cast<std::uint64_t>(producerIndex) + 1,
                                0,
                                producerTimeUs);
                            snapshot.state.leftStick.x = static_cast<float>(producerIndex % 101) / 100.0f;
                            Require(hub.PushPadSnapshot(snapshot, false), "rate-matrix analog publication must succeed");
                            ++producerIndex;
                            continue;
                        }

                        const auto capture = hub.Capture(64);
                        Require(capture.events.empty(), "rate-matrix pure analog input must never enter ordered queue");
                        if (capture.latestPadState) {
                            Require(
                                capture.latestPadState->generation >= lastObservedGeneration,
                                "rate-matrix owner must never observe a regressing latest generation");
                            Require(
                                capture.latestPadState->sourceTimestampUs <= ownerTimeUs,
                                "rate-matrix owner must never observe future analog state");
                            lastObservedGeneration = capture.latestPadState->generation;
                            semanticLatencyUs.push_back(ownerTimeUs - capture.latestPadState->sourceTimestampUs);
                        }
                        ++ownerIndex;
                    }

                    const auto finalCapture = hub.Capture(64);
                    Require(finalCapture.events.empty(), "final rate-matrix capture must retain zero ordered analog events");
                    Require(
                        finalCapture.latestPadState && finalCapture.latestPadState->generation == producerHz,
                        "rate-matrix final capture must publish the final complete producer generation");
                    const auto producerPeriodCeilingUs = (1'000'000ull + producerHz - 1) / producerHz;
                    Require(
                        percentile(semanticLatencyUs, 99) <= producerPeriodCeilingUs,
                        "rate-matrix semantic P99 must stay within one producer period");
                    Require(hub.PendingCount() == 0, "rate-matrix analog input must have zero queue amplification");
                }
            }
        }
    }

    void RecordAxisPublicationLatencyPercentiles()
    {
        constexpr std::size_t kIterations = 20'000;
        ingress::IngressHub hub{ 16 };
        Require(hub.PushPadSnapshot(LiveHidSnapshot(1, 0, 1), false), "latency benchmark baseline must publish");
        (void)hub.Capture(64);
        std::vector<std::uint64_t> latencyNs;
        latencyNs.reserve(kIterations);
        for (std::size_t index = 0; index < kIterations; ++index) {
            auto snapshot = LiveHidSnapshot(index + 2, 0, index + 2);
            snapshot.state.leftStick.x = static_cast<float>(index % 101) / 100.0f;
            const auto started = std::chrono::steady_clock::now();
            Require(hub.PushPadSnapshot(snapshot, false), "latency benchmark analog publication must succeed");
            const auto capture = hub.Capture(0);
            const auto finished = std::chrono::steady_clock::now();
            Require(
                capture.latestPadState && capture.latestPadState->generation == index + 2,
                "latency benchmark must observe each complete generation");
            latencyNs.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count()));
        }
        std::sort(latencyNs.begin(), latencyNs.end());
        const auto at = [&latencyNs](std::size_t percentile) {
            return latencyNs[std::min(
                latencyNs.size() - 1,
                (latencyNs.size() * percentile + 99) / 100 - 1)];
        };
        std::cout << "[DualPad][IngressBenchmark] axis_publication_latency_ns"
                  << " samples=" << latencyNs.size()
                  << " p50=" << at(50)
                  << " p95=" << at(95)
                  << " p99=" << at(99) << '\n';
    }

    void InputRecoveryBuildsScopedEpochAndSessionRequestFixture()
    {
        const auto global = runtime::BuildInputRecoveryRequest(runtime::InputRecoveryObservation{
            .marker = ingress::InputResetMarker{
                .reasons = ingress::ToMask(ingress::InputResetReason::ContextBoundary),
                .scope = ingress::InputResetScope::GlobalInputState,
                .inputStateEpoch = 8,
                .gamepadSessionId = 3,
                .contextRevision = 11,
                .controlMapRevision = 5 },
            .lastAppliedInputStateEpoch = 7,
            .lastAppliedGamepadSessionId = 3
        });
        Require(global.valid && global.clearAllVirtualOutput &&
                global.quarantineKeyboardMouse && global.resetSyntheticSuppression &&
                global.nextInputStateEpoch == 8,
            "global recovery must advance epoch and clear/quarantine every virtual input domain");

        const auto stale = runtime::BuildInputRecoveryRequest(runtime::InputRecoveryObservation{
            .marker = ingress::InputResetMarker{
                .scope = ingress::InputResetScope::GlobalInputState,
                .inputStateEpoch = 7,
                .gamepadSessionId = 3 },
            .lastAppliedInputStateEpoch = 7,
            .lastAppliedGamepadSessionId = 3
        });
        Require(!stale.valid && stale.failure == runtime::InputRecoveryFailure::StaleInputStateEpoch,
            "global recovery must reject a non-advancing epoch");

        const auto disconnect = runtime::BuildInputRecoveryRequest(runtime::InputRecoveryObservation{
            .marker = ingress::InputResetMarker{
                .reasons = ingress::ToMask(ingress::InputResetReason::DeviceDisconnected),
                .scope = ingress::InputResetScope::GamepadSource,
                .inputStateEpoch = 7,
                .gamepadSessionId = 4,
                .contextRevision = 11,
                .controlMapRevision = 5 },
            .lastAppliedInputStateEpoch = 7,
            .lastAppliedGamepadSessionId = 3
        });
        Require(disconnect.valid && disconnect.clearGamepadOutput &&
                !disconnect.clearKeyboardMouseOutput &&
                !disconnect.quarantineKeyboardMouse &&
                disconnect.nextInputStateEpoch == 7 &&
                disconnect.nextGamepadSessionId == 4,
            "gamepad disconnect must advance only session and preserve KBM domain");
    }

    void HubPublishesAcceptedScopedRecoveryRequestsFixture()
    {
        auto& mailbox = runtime::InputRecoveryMailbox::GetSingleton();
        mailbox.ResetForTests();

        ingress::IngressHub globalHub{ 16 };
        const auto globalReceipt = globalHub.PublishGlobalReset(
            ingress::ToMask(ingress::InputResetReason::ControlMapReload),
            ingress::InputResetScope::GlobalInputState);
        Require(globalReceipt.accepted, "global reset must be accepted before recovery publication");
        const auto globalRequests = mailbox.ConsumeAll();
        Require(globalRequests.size() == 1,
            "accepted global reset must publish exactly one recovery request");
        Require(globalRequests.front().valid && globalRequests.front().clearAllVirtualOutput &&
                globalRequests.front().quarantineKeyboardMouse &&
                globalRequests.front().previousInputStateEpoch == 1 &&
                globalRequests.front().nextInputStateEpoch == 2,
            "global recovery request must retain the accepted epoch transaction");

        ingress::IngressHub gamepadHub{ 16 };
        ingress::ClassifiedGamepadReportDraft connected{};
        connected.current.state.connected = true;
        connected.producerGamepadSessionId = 0;
        Require(gamepadHub.PublishGamepadBatch(
                    std::move(connected),
                    ingress::GamepadConnectionDraft{
                        .connectivity = ingress::GamepadConnectivity::Connected })
                    .accepted,
            "recovery fixture gamepad must connect");
        const auto disconnectReceipt = gamepadHub.PublishGamepadDisconnect();
        Require(disconnectReceipt.accepted, "recovery fixture disconnect must be accepted");
        const auto disconnectRequests = mailbox.ConsumeAll();
        Require(disconnectRequests.size() == 1,
            "accepted gamepad disconnect must publish exactly one recovery request");
        Require(disconnectRequests.front().valid && disconnectRequests.front().clearGamepadOutput &&
                !disconnectRequests.front().clearKeyboardMouseOutput &&
                !disconnectRequests.front().quarantineKeyboardMouse &&
                disconnectRequests.front().previousGamepadSessionId + 1 ==
                    disconnectRequests.front().nextGamepadSessionId,
            "disconnect recovery request must advance only gamepad session");

        mailbox.ResetForTests();
        ingress::IngressHub overflowHub{ 1 };
        Require(overflowHub.PushEvent(Manifest(1)), "overflow recovery fixture must fill the queue");
        Require(!overflowHub.PushEvent(Manifest(2)), "overflow recovery fixture must compact the queue");
        const auto overflowRequests = mailbox.ConsumeAll();
        Require(overflowRequests.size() == 1 &&
                overflowRequests.front().clearAllVirtualOutput &&
                overflowRequests.front().quarantineKeyboardMouse &&
                overflowRequests.front().reasons ==
                    ingress::ToMask(ingress::InputResetReason::QueueOverflow),
            "queue overflow must publish one global recovery transaction");
        mailbox.ResetForTests();
    }
}

int main()
{
    TestHubAssignsSeqAndEmitsOverflowMarker();
    TestHubDrainHonorsExactEventBudget();
    BatchApiCompileFixture();
    BatchCapacityAtomicityFixture();
    CumulativeEmptyCaptureFixture();
    PartialDrainCausalTailFixture();
    LatestOnlyNoFakeSeqFixture();
    EmptyCaptureAppliesReadyLatestFixture();
    DisconnectScopeKeepsKbmAndGlobalEpochFixture();
    OldGamepadSessionDropFixture();
    OverflowDoesNotReattachOldHeldFixture();
    AtomicBoundaryFirstBatchFixture();
    SequenceGapRequestsGlobalEpochResetFixture();
    RevisionAheadFixture();
    NeutralHidInterleaveFixture();
    HeldStickUnchangedFixture();
    ReleaseDoesNotTakeoverFixture();
    ConnectivityOnlyFixture();
    ContextNeutralGamepadDraftFixture();
    ClassifiedDigitalEdgeFeedsExistingKernelFixture();
    OrderedMeaningfulActivityReachesOneStableFrameFixture();
    KbmProducerPublishesMappedCurrentAndOrderedFactsFixture();
    KbmSyntheticSuppressionRequiresExactProvenanceFixture();
    KbmMappingChangeUsesStablePhysicalQuarantineFixture();
    KbmExplicitRecoveryQuarantinesAndResetsSyntheticFixture();
    KbmOptionalRawReconcileRequiresCompletePhysicalProofFixture();
    KbmMappingBoundaryClearsOldSyntheticReceiptFixture();
    InputRecoveryBuildsScopedEpochAndSessionRequestFixture();
    HubPublishesAcceptedScopedRecoveryRequestsFixture();
    TestLatestPadStatePreventsSteadyAnalogQueueGrowth();
    TestLatestAnalogCanLeadBoundedDigitalEdgeCutoff();
    TestFrameAssemblerDefersLatestAnalogBeyondOrderedEdgeCutoff();
    TestOverflowRetainsLatestPadState();
    TestOverflowFreezesDigitalEdgesUntilCleanRelease();
    TestSourceEvidenceUsesLatestPublicationWithoutQueueGrowth();
    TestConcurrentCaptureNeverObservesHalfHidTransaction();
    TestLatestSourceEvidenceCannotBypassQueuedDeviceBoundary();
    TestLatestSourceEvidenceAheadOfCapturedBoundaryWaitsForMatchingMarker();
    TestOrderedPadEventWaitsForDeviceSourcePair();
    TestLatestAnalogCannotBypassQueuedContextBoundary();
    TestHubOverflowCompactsBoundaryFactsAndDropsVolatileInput();
    TestLegacySnapshotAdapterProducesControlSamplesAndPulseLedger();
    TestLegacySnapshotAdapterPrefersInputV2ContextRevision();
    TestLegacySnapshotBatchOverflowRejectsPartialEvents();
    TestRejectedLegacySnapshotAdvancesWatermarkAsDroppedRange();
    TestLegacySequenceDiscontinuityProducesSequenceGap();
    TestLiveHidMaskEdgesProducePulseLedger();
    TestLiveHidPressSampleTriggersInteractionEngine();
    TestCoalescedHeldHidPressSampleTriggersInteractionEngine();
    TestPublishedPressEdgeDoesNotCarryIntoNextStableFrame();
    TestPublishedReleaseEdgeDoesNotCarryIntoNextStableFrame();
    TestManifestPublisherProducesIngressMarker();
    TestDeviceFamilyProducerProducesMarkerAndPairedSourceEvidence();
    TestLiveGamepadInputPublishesSourceEvidence();
    TestLiveKeyboardMouseEvidencePublishesTakeoverAndReclaim();
    TestSyntheticKeyboardWindowDoesNotPublishKeyboardMouseTakeover();
    TestStableMergeKeepsPulseLedger();
    TestBoundaryChangeFlushesStableThenTransition();
    TestRecoveryMarkersMapFailClosed();
    TestDeviceReportSequenceGapIsSoftDiagnosticAndKeepsDigitalEdge();
    TestCoalescedHidReportsDoNotHardResetOutputs();
    TestRuntimeSnapshotSeqGapWithoutBoundaryChangeIsSoftTransition();
    TestAxisOnlyCoalescingDoesNotClearHeldButton();
    TestFrameAssemblerDoesNotSortOutOfOrderEvents();
    TestFrameAssemblerUsesIngressSeqForMonotonicTimeRegression();
    TestDeviceFamilyEvidenceFromIndependentThreadsUsesIngressSeqAuthority();
    TestFrameAssemblerOverflowPayloadBuildsBoundaryBaseline();
    TestDeviceMarkerMismatchFailsClosed();
    TestStaleSourceEvidenceAfterNewerDeviceMarkerDoesNotHardReset();
    TestMissingDeviceMarkerSourceEvidenceSoftSyncsBoundary();
    TestBuildKernelFrameDoesNotAcceptTransition();
    TestLegacySnapshotCannotOverrideKernelFacts();
    TestBuildKernelFrameUsesIngressMonotonicTimestamp();
    TestDeterministicProducerOwnerRateMatrix();
    RecordAxisPublicationLatencyPercentiles();
    std::cout << "DualPadIngressTests passed\n";
    return 0;
}
