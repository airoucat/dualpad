#pragma once

#include <filesystem>

namespace dualpad::input
{
    class ControlMapOverlay
    {
    public:
        static ControlMapOverlay& GetSingleton();

        bool Apply(const std::filesystem::path& path = {});
        bool Reapply();

        static std::filesystem::path GetDefaultPath();

    private:
        ControlMapOverlay() = default;

        std::filesystem::path _overlayPath;
    };
}
