#pragma once

#include <filesystem>

namespace dualpad::input
{
    enum class UpstreamGamepadHookMode
    {
        Disabled = 0,
        PollXInputCall
    };

    class RuntimeConfig
    {
    public:
        static RuntimeConfig& GetSingleton();

        bool Load(const std::filesystem::path& path = {});
        bool Reload();
        static std::filesystem::path GetDefaultPath();

        bool LogInputPackets() const { return _logInputPackets; }
        bool LogInputHex() const { return _logInputHex; }
        bool LogInputState() const { return _logInputState; }
        bool LogMappingEvents() const { return _logMappingEvents; }
        bool LogSyntheticState() const { return _logSyntheticState; }
        bool LogActionPlan() const { return _logActionPlan; }
        bool LogNativeInjection() const { return _logNativeInjection; }
        bool LogKeyboardInjection() const { return _logKeyboardInjection; }

        bool UseUpstreamGamepadHook() const { return _useUpstreamGamepadHook; }
        UpstreamGamepadHookMode GetUpstreamGamepadHookMode() const { return _upstreamGamepadHookMode; }

    private:
        RuntimeConfig() = default;

        bool ParseIniFile(const std::filesystem::path& path);
        void ResetToDefaults();

        std::filesystem::path _configPath;

        bool _logInputPackets{ false };
        bool _logInputHex{ false };
        bool _logInputState{ false };
        bool _logMappingEvents{ false };
        bool _logSyntheticState{ false };
        bool _logActionPlan{ false };
        bool _logNativeInjection{ false };
        bool _logKeyboardInjection{ false };

        bool _useUpstreamGamepadHook{ true };
        UpstreamGamepadHookMode _upstreamGamepadHookMode{ UpstreamGamepadHookMode::PollXInputCall };
    };
}
