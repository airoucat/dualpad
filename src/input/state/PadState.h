#pragma once

#include "input/protocol/DualSenseProtocolTypes.h"

#include <cstdint>

namespace dualpad::input
{
    struct TouchPointState
    {
        bool active{ false };
        std::uint16_t x{ 0 };
        std::uint16_t y{ 0 };
        std::uint8_t id{ 0 };
    };

    struct StickState
    {
        std::uint8_t rawX{ 0x80 };
        std::uint8_t rawY{ 0x80 };
        float x{ 0.0f };
        float y{ 0.0f };
    };

    struct TriggerState
    {
        std::uint8_t raw{ 0 };
        float normalized{ 0.0f };
    };

    struct ImuState
    {
        std::int16_t gyroX{ 0 };
        std::int16_t gyroY{ 0 };
        std::int16_t gyroZ{ 0 };
        std::int16_t accelX{ 0 };
        std::int16_t accelY{ 0 };
        std::int16_t accelZ{ 0 };
        bool valid{ false };
    };

    struct PadButtons
    {
        std::uint32_t digitalMask{ 0 };
        bool touchpadClick{ false };
        bool mute{ false };
        bool ps{ false };
    };

    struct PadState
    {
        bool connected{ false };
        TransportType transport{ TransportType::Unknown };
        std::uint8_t reportId{ 0 };
        std::uint64_t timestampUs{ 0 };
        std::uint64_t sequence{ 0 };

        PadButtons buttons{};
        StickState leftStick{};
        StickState rightStick{};
        TriggerState leftTrigger{};
        TriggerState rightTrigger{};

        TouchPointState touch1{};
        TouchPointState touch2{};

        ImuState imu{};

        std::uint8_t battery{ 0 };
        bool batteryValid{ false };
    };

    bool HasTouchData(const PadState& state);
    bool HasImuData(const PadState& state);
}
