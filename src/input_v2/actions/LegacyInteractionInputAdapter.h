#pragma once

#include "input_v2/actions/ControlPath.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace dualpad::input_v2::actions
{
    struct ControlSample
    {
        ControlPath path;
        bool down{ false };
        bool pressed{ false };
        bool released{ false };
        float scalar{ 0.0f };
        std::uint64_t downAtUs{ 0 };
        std::uint64_t timestampUs{ 0 };
    };

    struct KernelFacts
    {
        std::uint64_t manifestEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint32_t deviceFamilyRevision{ 0 };
        std::uint64_t monotonicUs{ 0 };
    };

    struct KernelState
    {
        std::vector<ControlSample> controlSamples;
        bool cleanBoundaryBaseline{ true };
        bool healthDegraded{ false };
    };

    struct KernelFrame
    {
        KernelFacts facts;
        KernelState state;
        std::uint64_t kernelRevision{ 0 };
    };

    struct LegacyInteractionInputFrame
    {
        std::uint64_t manifestEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint32_t deviceFamilyRevision{ 0 };
        std::uint64_t monotonicUs{ 0 };
        std::vector<ControlSample> samples;
    };

    // Temporary PH4 adapter while Phase 7 InputKernel is not the runtime producer yet.
    // It only copies existing control samples and boundary revisions; it does not run
    // legacy tap/hold, combo, binding resolution, owner inference, prompt logic,
    // or recovery logic. Delete once InputKernel::BuildKernelFrame consumes
    // AssembledFactFrame directly.
    class LegacyInteractionInputAdapter
    {
    public:
        static KernelFrame BuildKernelFrame(const LegacyInteractionInputFrame& legacyFrame);
        static std::string_view DeletionCondition();
    };
}
