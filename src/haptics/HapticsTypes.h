#pragma once

#include <chrono>
#include <cstdint>

namespace dualpad::haptics
{
    using TimePoint = std::chrono::steady_clock::time_point;

    inline TimePoint Now() { return std::chrono::steady_clock::now(); }

    inline std::uint64_t ToQPC(TimePoint tp)
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count());
    }

    struct HidFrame
    {
        std::uint64_t qpc{ 0 };
        std::uint8_t leftMotor{ 0 };
        std::uint8_t rightMotor{ 0 };
    };
}
