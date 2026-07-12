#pragma once

#include "input_v2/gameplay/CurrentCycleGatePlan.h"

#include <RE/Skyrim.h>

#include <cstddef>
#include <vector>

namespace dualpad::input
{
    using CurrentCycleAdapterAudit = input_v2::gameplay::CurrentCycleAdapterAudit;

    struct CurrentCycleEventDescriptor
    {
        input_v2::gameplay::CurrentCycleChannel channel{
            input_v2::gameplay::CurrentCycleChannel::Look
        };
        bool virtualEvent{ false };
    };

    struct CurrentCycleAdapterOptions
    {
        bool shadowOnly{ true };
        bool consumerOrderProven{ false };
        std::size_t scratchCapacity{ 64 };
    };

    class SkyrimCurrentCycleEventAdapter
    {
    public:
        CurrentCycleAdapterAudit AuditDescriptors(
            std::vector<CurrentCycleEventDescriptor>& descriptors,
            const input_v2::gameplay::CurrentCycleGatePlan& plan,
            const CurrentCycleAdapterOptions& options) const;
        CurrentCycleAdapterAudit AuditEventListShadow(
            RE::InputEvent* const* events,
            const input_v2::gameplay::CurrentCycleGatePlan& plan,
            std::size_t scratchCapacity = 64) const;

        static constexpr bool ProductionMutationEnabled() noexcept { return false; }
    };
}
