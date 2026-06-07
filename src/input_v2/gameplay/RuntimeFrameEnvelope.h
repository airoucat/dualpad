#pragma once

#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/FrameAssembler.h"

#include <cstdint>
#include <memory>
#include <string>

namespace dualpad::input_v2::config
{
    struct CompiledConfigBundle;
}

namespace dualpad::input_v2::gameplay
{
    enum class RuntimeHealthReason : std::uint32_t
    {
        None = 0,
        GraphUnavailable = 1u << 0,
        ManifestEpochSkew = 1u << 1,
        ContextRevisionSkew = 1u << 2,
        QueueOverflow = 1u << 3,
        SequenceGap = 1u << 4,
        BoundaryMismatch = 1u << 5,
        PromptScopeFrozen = 1u << 6,
        HookInstallFailed = 1u << 7
    };

    using RuntimeHealthReasonMask = std::uint32_t;

    constexpr RuntimeHealthReasonMask RuntimeHealthMask(RuntimeHealthReason reason)
    {
        return static_cast<RuntimeHealthReasonMask>(reason);
    }

    constexpr RuntimeHealthReasonMask AddRuntimeHealthReason(
        RuntimeHealthReasonMask mask,
        RuntimeHealthReason reason)
    {
        return mask | RuntimeHealthMask(reason);
    }

    constexpr bool HasRuntimeHealthReason(RuntimeHealthReasonMask mask, RuntimeHealthReason reason)
    {
        return (mask & RuntimeHealthMask(reason)) != 0;
    }

    struct RuntimeConfigSnapshot
    {
        std::shared_ptr<const config::CompiledConfigBundle> bundle;
        actions::PublishedActionGraphSnapshot graph;
        context::ResolvedContextSnapshot context;
        std::uint64_t manifestEpoch{ 0 };
        std::uint64_t configGeneration{ 0 };
    };

    struct FrameRuntimeEnvelope
    {
        ingress::AssembledFactFrame frame;
        RuntimeConfigSnapshot config;
        RuntimeHealthReasonMask healthReasons{ RuntimeHealthMask(RuntimeHealthReason::None) };
        std::string debugReason;

        [[nodiscard]] bool RuntimeHealthDegraded() const
        {
            return healthReasons != RuntimeHealthMask(RuntimeHealthReason::None);
        }
    };
}
