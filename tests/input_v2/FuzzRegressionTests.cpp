#include "pch.h"

#include "input/PadEvent.h"
#include "input/injection/PadEventSnapshot.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/ingress/LegacyIngressAdapter.h"

#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }
}

int main()
{
    using namespace dualpad::input;
    using namespace dualpad::input_v2::ingress;

    FrameAssembler assembler;
    for (std::uint64_t seq = 1; seq <= 32; ++seq) {
        PadEventSnapshot snapshot{};
        snapshot.type = PadEventSnapshotType::Input;
        snapshot.firstSequence = seq;
        snapshot.sequence = seq;
        snapshot.sourceTimestampUs = seq * 1000;
        snapshot.context = InputContext::Gameplay;
        snapshot.contextEpoch = 1;
        snapshot.state.sequence = seq;
        snapshot.state.timestampUs = snapshot.sourceTimestampUs;
        snapshot.state.buttons.digitalMask = (seq % 2) == 0 ? 0x1u : 0u;

        if ((seq % 3) == 0) {
            PadEvent event{};
            event.type = PadEventType::ButtonPress;
            event.code = 0x1u;
            event.timestampUs = snapshot.sourceTimestampUs;
            Require(snapshot.events.Push(event), "fuzz event should fit");
        }

        const auto events = ConvertLegacySnapshotToIngressEvents(snapshot, seq - 1);
        const auto frames = assembler.Assemble(events);
        Require(!frames.empty(), "fuzz snapshot should assemble without dropping all frames");
    }

    IngressHub hub{ 8 };
    FrameAssembler latestAssembler;
    std::uint32_t random = 0xC0FFEEu;
    std::uint32_t mask = 0;
    std::uint64_t lastLatestGeneration = 0;
    for (std::uint64_t seq = 1; seq <= 2048; ++seq) {
        random = random * 1664525u + 1013904223u;
        if ((random & 0xFu) == 0) {
            mask ^= 1u << ((random >> 8) & 0x3u);
        }

        PadEventSnapshot snapshot{};
        snapshot.type = PadEventSnapshotType::Input;
        snapshot.firstSequence = seq;
        snapshot.sequence = seq;
        snapshot.sourceTimestampUs = seq * 100;
        snapshot.state.sequence = seq;
        snapshot.state.timestampUs = snapshot.sourceTimestampUs;
        snapshot.state.buttons.digitalMask = mask;
        snapshot.state.leftStick.x = static_cast<float>(static_cast<std::int32_t>(random & 0xFFu) - 127) / 127.0f;
        (void)hub.PushPadSnapshot(snapshot, false);
        Require(hub.PendingCount() <= 8, "fuzz ordered edge queue must remain bounded");

        if ((random & 0x7u) == 0) {
            const auto capture = hub.Capture((random >> 4) & 0x3u);
            Require(capture.latestPadState.has_value(), "fuzz capture must retain a complete latest pad publication");
            Require(capture.latestPadState->generation >= lastLatestGeneration, "fuzz latest generation must never regress");
            lastLatestGeneration = capture.latestPadState->generation;
            (void)latestAssembler.Assemble(
                capture.events,
                capture.latestPadState,
                capture.latestSourceEvidence);
        }
    }
    const auto finalCapture = hub.Capture(8);
    Require(finalCapture.latestPadState && finalCapture.latestPadState->generation == 2048, "fuzz latest publication must end at the final complete report");
    (void)latestAssembler.Assemble(
        finalCapture.events,
        finalCapture.latestPadState,
        finalCapture.latestSourceEvidence);

    return 0;
}
