#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace dualpad::input_v2::telemetry
{
    inline constexpr std::uint32_t kTraceSchemaVersion = 1;

    struct TraceFileSpec
    {
        std::string_view name;
        std::string_view header;
    };

    std::span<const TraceFileSpec> Phase0TraceFiles();
    const TraceFileSpec* FindPhase0TraceFile(std::string_view fileName);
}
