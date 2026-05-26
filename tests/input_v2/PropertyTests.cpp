#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"

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
    using namespace dualpad::input_v2::ingress;

    IngressHub hub{ 8 };
    for (std::uint64_t i = 0; i < 4; ++i) {
        IngressEvent event{};
        event.kind = IngressKind::PadSnapshot;
        event.source = IngressSource::LegacyDispatcher;
        event.monotonicUs = i + 1;
        Require(hub.PushEvent(std::move(event)), "push should fit bounded property hub");
    }

    auto drained = hub.Drain();
    Require(drained.size() == 4, "property hub should drain all pushed events");
    for (std::size_t i = 1; i < drained.size(); ++i) {
        Require(drained[i - 1].seq < drained[i].seq, "ingress seq must be strictly monotonic");
    }

    FrameAssembler assembler;
    const auto frames = assembler.Assemble(drained);
    Require(!frames.empty(), "property assembler should publish at least one frame");
    for (const auto& frame : frames) {
        Require(frame.firstSeq <= frame.lastSeq, "assembled frame sequence range must be ordered");
    }

    return 0;
}
