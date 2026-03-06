#pragma once

#include <cstdint>
#include <filesystem>

namespace dualpad::haptics
{
    struct HapticsConfig
    {
        bool enabled{ true };
        float leftMotorScale{ 1.0f };
        float rightMotorScale{ 1.0f };
        float maxIntensity{ 1.0f };
        float deadzone{ 0.0f };
        bool logNativeVibration{ false };
        std::uint32_t statsIntervalMs{ 1000 };

        static HapticsConfig& GetSingleton();
        bool Load(const std::filesystem::path& path = {});
        static std::filesystem::path GetDefaultPath();

    private:
        HapticsConfig() = default;

        bool ParseIniFile(const std::filesystem::path& path);
    };
}
