#pragma once

#include "input_v2/actions/CompiledActionGraph.h"

#include <cstdint>

namespace dualpad::input_v2::gameplay
{
    struct TransientDedupKey
    {
        actions::ActionId actionId{};
        std::uint64_t materializationToken{ 0 };
        std::uint32_t contextRevision{ 0 };

        friend bool operator==(const TransientDedupKey&, const TransientDedupKey&) = default;
    };

    enum class TransientGateDisposition : std::uint8_t
    {
        Keep = 0,
        Suppress,
        Cancel
    };

    struct TransientActionGateState
    {
        TransientDedupKey key{};
        bool hasKey{ false };
        bool physicalDown{ false };
        bool virtualDownVisible{ false };
    };

    struct TransientActionGateInput
    {
        TransientActionGateState previous{};
        TransientDedupKey key{};
        bool physicalPress{ false };
        bool physicalRelease{ false };
        bool virtualPress{ false };
        bool virtualRelease{ false };
        bool virtualDownVisible{ false };
    };

    struct TransientActionGateDecision
    {
        TransientGateDisposition physicalDisposition{ TransientGateDisposition::Keep };
        TransientGateDisposition virtualDisposition{ TransientGateDisposition::Keep };
        TransientActionGateState next{};
    };

    TransientActionGateDecision ResolveTransientActionGate(
        const TransientActionGateInput& input);
}
