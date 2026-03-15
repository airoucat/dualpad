#pragma once

#include <filesystem>

namespace dualpad::input
{
    enum class NativeButtonHookMode
    {
        DropProbe,
        AppendProbe,
        Append,
        HeadPrepend,
        Inject
    };

    enum class UpstreamGamepadHookMode
    {
        Disabled = 0,
        PollXInputCall
    };

    enum class UpstreamKeyboardHookMode
    {
        SemanticMid = 0,
        DiObjDataCall
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
        bool UseUpstreamKeyboardHook() const { return _useUpstreamKeyboardHook; }
        UpstreamKeyboardHookMode GetUpstreamKeyboardHookMode() const { return _upstreamKeyboardHookMode; }
        bool TestKeyboardEventSourcePatch() const { return _testKeyboardEventSourcePatch; }
        bool TestKeyboardManagerHeadPatch() const { return _testKeyboardManagerHeadPatch; }
        bool TestKeyboardAcceptDumpRoute() const { return _testKeyboardAcceptDumpRoute; }
        bool UseNativeButtonInjector() const { return _useNativeButtonInjector; }
        bool UseNativeFrameInjector() const { return _useNativeFrameInjector; }
        NativeButtonHookMode GetNativeButtonHookMode() const { return _nativeButtonHookMode; }
        bool UseNativeButtonDropProbe() const { return _nativeButtonHookMode == NativeButtonHookMode::DropProbe; }

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
        bool _useUpstreamKeyboardHook{ false };
        UpstreamKeyboardHookMode _upstreamKeyboardHookMode{ UpstreamKeyboardHookMode::SemanticMid };
        bool _testKeyboardEventSourcePatch{ false };
        bool _testKeyboardManagerHeadPatch{ false };
        bool _testKeyboardAcceptDumpRoute{ false };
        bool _useNativeButtonInjector{ false };
        bool _useNativeFrameInjector{ false };
        NativeButtonHookMode _nativeButtonHookMode{ NativeButtonHookMode::DropProbe };
    };
}
