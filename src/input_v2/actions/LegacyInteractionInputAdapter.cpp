#include "pch.h"

#include "input_v2/actions/LegacyInteractionInputAdapter.h"

namespace dualpad::input_v2::actions
{
    KernelFrame LegacyInteractionInputAdapter::BuildKernelFrame(const LegacyInteractionInputFrame& legacyFrame)
    {
        KernelFrame frame{};
        frame.facts.manifestEpoch = legacyFrame.manifestEpoch;
        frame.facts.contextRevision = legacyFrame.contextRevision;
        frame.facts.menuStackRevision = legacyFrame.menuStackRevision;
        frame.facts.deviceFamilyRevision = legacyFrame.deviceFamilyRevision;
        frame.facts.monotonicUs = legacyFrame.monotonicUs;
        frame.state.controlSamples = legacyFrame.samples;
        frame.kernelRevision =
            legacyFrame.manifestEpoch ^
            (static_cast<std::uint64_t>(legacyFrame.contextRevision) << 16) ^
            (static_cast<std::uint64_t>(legacyFrame.menuStackRevision) << 32) ^
            (static_cast<std::uint64_t>(legacyFrame.deviceFamilyRevision) << 48) ^
            legacyFrame.monotonicUs;
        return frame;
    }

    std::string_view LegacyInteractionInputAdapter::DeletionCondition()
    {
        return "Delete once InputKernel::BuildKernelFrame consumes AssembledFactFrame directly; do not carry this adapter beyond Phase 7.";
    }
}
