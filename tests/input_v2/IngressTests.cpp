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

    void TestFrameAssemblerKeepsLatestAnalogSeparateFromOrderedEdgeCutoff()
    {
        ingress::IngressHub hub{ 16 };
        ingress::FrameAssembler assembler;
        Require(hub.PushPadSnapshot(LiveHidSnapshot(1, 0, 100), false), "assembler baseline must publish");
        auto capture = hub.Capture(16);
        (void)assembler.Assemble(capture.events, capture.latestPadState, capture.latestSourceEvidence);

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
        auto frames = assembler.Assemble(capture.events, capture.latestPadState, capture.latestSourceEvidence);
        const auto& pressFrame = LastStableFrame(frames);
        const auto* pressSample = FindControlSample(pressFrame.facts, 0x1);
        const auto* axisSample = FindAxisSample(pressFrame.facts, input::PadAxisId::LeftStickX);
        Require(pressSample && pressSample->pressed && !pressSample->released, "first cutoff frame must contain only the ordered press edge");
        Require(axisSample && axisSample->scalar == 0.9f, "first cutoff frame may contain newer latest analog state");
        Require(FindPulse(pressFrame.facts, 0x1, false, true) == nullptr, "future release mask must not synthesize an early release");
        const auto pressMonotonicUs = pressFrame.facts.monotonicUs;

        capture = hub.Capture(1);
        frames = assembler.Assemble(capture.events, capture.latestPadState, capture.latestSourceEvidence);
        const auto& releaseFrame = LastStableFrame(frames);
        Require(FindPulse(releaseFrame.facts, 0x1, false, true) != nullptr, "second cutoff frame must deliver the queued release edge");
        Require(releaseFrame.facts.monotonicUs >= pressMonotonicUs, "older queued release must not regress stable fact time after latest analog led the cutoff");
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
}

int main()
{
    TestHubAssignsSeqAndEmitsOverflowMarker();
    TestHubDrainHonorsExactEventBudget();
    TestLatestPadStatePreventsSteadyAnalogQueueGrowth();
    TestLatestAnalogCanLeadBoundedDigitalEdgeCutoff();
    TestFrameAssemblerKeepsLatestAnalogSeparateFromOrderedEdgeCutoff();
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
