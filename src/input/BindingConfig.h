#pragma once
#include "input/mapping/TouchpadMapper.h"

#include <string>
#include <filesystem>

namespace dualpad::input
{
    struct Trigger;

    // Loads INI-defined bindings into BindingManager.
    class BindingConfig
    {
    public:
        static BindingConfig& GetSingleton();

        // Uses the default path when path is empty.
        bool Load(const std::filesystem::path& path = {});

        bool Reload();

        static std::filesystem::path GetDefaultPath();
        TouchpadConfig GetTouchpadConfig() const;

    private:
        BindingConfig() = default;

        std::filesystem::path _configPath;
        TouchpadConfig _touchpadConfig{};

        bool ParseIniFile(const std::filesystem::path& path);
        bool ParseBinding(std::string_view context, std::string_view key, std::string_view value);
        bool ParseTouchpadSetting(std::string_view key, std::string_view value);
        // Writes the parsed trigger into outTrigger on success.
        bool ParseTrigger(std::string_view triggerStr, Trigger& outTrigger);
    };
}
