#include "pch.h"

#include "input/PadEvent.h"
#include "input/injection/PadEventSnapshot.h"
#include "input_v2/ingress/FrameAssembler.h"
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

    return 0;
}
