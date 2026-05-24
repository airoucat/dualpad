#pragma once

#include <cstdint>

namespace dualpad::input_v2::ingress
{
    struct IngressBoundaryKey
    {
        std::uint32_t manifestEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint32_t deviceFamilyRevision{ 0 };

        friend bool operator==(const IngressBoundaryKey&, const IngressBoundaryKey&) = default;
    };
}
