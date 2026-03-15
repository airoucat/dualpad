#include "pch.h"
#include "input/RuntimeConfig.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        inline std::string Trim(std::string s)
        {
            auto isSpace = [](unsigned char c) {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            };

            while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) {
                s.erase(s.begin());
            }
            while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) {
                s.pop_back();
            }
            return s;
        }

        inline std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        inline bool ParseBool(const std::string& value, bool defaultValue = false)
        {
            const auto normalized = ToLower(Trim(value));
            if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
                return true;
            }
            if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
                return false;
            }
            return defaultValue;
        }

        inline NativeButtonHookMode ParseNativeButtonHookMode(
            const std::string& value,
            NativeButtonHookMode defaultValue)
        {
            const auto normalized = ToLower(Trim(value));
            if (normalized == "inject" || normalized == "prepend") {
                return NativeButtonHookMode::Inject;
            }
            if (normalized == "head-prepend" || normalized == "head_prepend" || normalized == "controlmap-only") {
                return NativeButtonHookMode::HeadPrepend;
            }
            if (normalized == "append" || normalized == "queue") {
                return NativeButtonHookMode::Append;
            }
            if (normalized == "append-probe" || normalized == "queue-probe" || normalized == "append_probe") {
                return NativeButtonHookMode::AppendProbe;
            }
            if (normalized == "drop" || normalized == "drop-probe" || normalized == "probe") {
                return NativeButtonHookMode::DropProbe;
            }
            return defaultValue;
        }

        inline const char* ToString(NativeButtonHookMode mode)
        {
            switch (mode) {
            case NativeButtonHookMode::Inject:
                return "inject";
            case NativeButtonHookMode::HeadPrepend:
                return "head-prepend";
            case NativeButtonHookMode::AppendProbe:
                return "append-probe";
            case NativeButtonHookMode::Append:
                return "append";
            case NativeButtonHookMode::DropProbe:
            default:
                return "drop";
            }
        }

        inline UpstreamGamepadHookMode ParseUpstreamGamepadHookMode(
            const std::string& value,
            UpstreamGamepadHookMode defaultValue)
        {
            const auto normalized = ToLower(Trim(value));
            if (normalized == "poll-vtable" || normalized == "poll_vtable" || normalized == "vtable") {
                return UpstreamGamepadHookMode::PollXInputCall;
            }
            if (normalized == "poll-xinput-call" || normalized == "poll_xinput_call" || normalized == "xinput-call") {
                return UpstreamGamepadHookMode::PollXInputCall;
            }
            if (normalized == "disabled" || normalized == "off" || normalized == "none") {
                return UpstreamGamepadHookMode::Disabled;
            }
            return defaultValue;
        }

        inline const char* ToString(UpstreamGamepadHookMode mode)
        {
            switch (mode) {
            case UpstreamGamepadHookMode::PollXInputCall:
                return "poll-xinput-call";
            case UpstreamGamepadHookMode::Disabled:
            default:
                return "disabled";
            }
        }

        inline UpstreamKeyboardHookMode ParseUpstreamKeyboardHookMode(
            const std::string& value,
            UpstreamKeyboardHookMode defaultValue)
        {
            const auto normalized = ToLower(Trim(value));
            if (normalized == "semantic-mid" || normalized == "semantic_mid" || normalized == "mid" || normalized == "semantic") {
                return UpstreamKeyboardHookMode::SemanticMid;
            }
            if (normalized == "diobjdata-call" || normalized == "diobjdata_call" || normalized == "diobjdata" || normalized == "call-site" || normalized == "callsite") {
                return UpstreamKeyboardHookMode::DiObjDataCall;
            }
            return defaultValue;
        }

        inline const char* ToString(UpstreamKeyboardHookMode mode)
        {
            switch (mode) {
            case UpstreamKeyboardHookMode::DiObjDataCall:
                return "diobjdata-call";
            case UpstreamKeyboardHookMode::SemanticMid:
            default:
                return "semantic-mid";
            }
        }
    }

    RuntimeConfig& RuntimeConfig::GetSingleton()
    {
        static RuntimeConfig instance;
        return instance;
    }

    std::filesystem::path RuntimeConfig::GetDefaultPath()
    {
        return "Data/SKSE/Plugins/DualPadDebug.ini";
    }

    bool RuntimeConfig::Load(const std::filesystem::path& path)
    {
        _configPath = path.empty() ? GetDefaultPath() : path;
        ResetToDefaults();

        logger::info("[DualPad][RuntimeConfig] Loading from: {}", _configPath.string());

        if (!std::filesystem::exists(_configPath)) {
            logger::warn("[DualPad][RuntimeConfig] Config file not found, using defaults");
            return false;
        }

        return ParseIniFile(_configPath);
    }

    bool RuntimeConfig::Reload()
    {
        if (_configPath.empty()) {
            return Load();
        }

        ResetToDefaults();
        return ParseIniFile(_configPath);
    }

    bool RuntimeConfig::ParseIniFile(const std::filesystem::path& path)
    {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            logger::error("[DualPad][RuntimeConfig] Failed to open: {}", path.string());
            return false;
        }

        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections;

        std::string section;
        std::string line;
        std::size_t lineNo = 0;

        while (std::getline(ifs, line)) {
            ++lineNo;

            if (lineNo == 1 && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }

            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                section = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            const auto eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                continue;
            }

            auto key = Trim(line.substr(0, eqPos));
            auto value = Trim(line.substr(eqPos + 1));
            if (key.empty()) {
                continue;
            }

            sections[section][key] = value;
        }

        const auto parseLogging = [&](const auto& values) {
            if (auto it = values.find("log_input_packets"); it != values.end()) {
                _logInputPackets = ParseBool(it->second, _logInputPackets);
            }
            if (auto it = values.find("log_input_hex"); it != values.end()) {
                _logInputHex = ParseBool(it->second, _logInputHex);
            }
            if (auto it = values.find("log_input_state"); it != values.end()) {
                _logInputState = ParseBool(it->second, _logInputState);
            }
            if (auto it = values.find("log_mapping_events"); it != values.end()) {
                _logMappingEvents = ParseBool(it->second, _logMappingEvents);
            }
            if (auto it = values.find("log_synthetic_state"); it != values.end()) {
                _logSyntheticState = ParseBool(it->second, _logSyntheticState);
            }
            if (auto it = values.find("log_action_plan"); it != values.end()) {
                _logActionPlan = ParseBool(it->second, _logActionPlan);
            }
            if (auto it = values.find("log_native_injection"); it != values.end()) {
                _logNativeInjection = ParseBool(it->second, _logNativeInjection);
            }
            if (auto it = values.find("log_keyboard_injection"); it != values.end()) {
                _logKeyboardInjection = ParseBool(it->second, _logKeyboardInjection);
            }
        };

        const auto parseInjection = [&](const auto& values) {
            if (auto it = values.find("use_upstream_gamepad_hook"); it != values.end()) {
                _useUpstreamGamepadHook = ParseBool(it->second, _useUpstreamGamepadHook);
            }
            if (auto it = values.find("upstream_gamepad_hook_mode"); it != values.end()) {
                const auto normalized = ToLower(Trim(it->second));
                if (normalized == "poll-vtable" || normalized == "poll_vtable" || normalized == "vtable") {
                    logger::warn(
                        "[DualPad][RuntimeConfig] upstream_gamepad_hook_mode='{}' is retired; using poll-xinput-call",
                        it->second);
                }
                _upstreamGamepadHookMode = ParseUpstreamGamepadHookMode(it->second, _upstreamGamepadHookMode);
            }
            if (auto it = values.find("use_upstream_keyboard_hook"); it != values.end()) {
                _useUpstreamKeyboardHook = ParseBool(it->second, _useUpstreamKeyboardHook);
            }
            if (auto it = values.find("upstream_keyboard_hook_mode"); it != values.end()) {
                _upstreamKeyboardHookMode = ParseUpstreamKeyboardHookMode(it->second, _upstreamKeyboardHookMode);
            }
            if (auto it = values.find("test_keyboard_event_source_patch"); it != values.end()) {
                _testKeyboardEventSourcePatch = ParseBool(it->second, _testKeyboardEventSourcePatch);
            }
            if (auto it = values.find("test_keyboard_manager_head_patch"); it != values.end()) {
                _testKeyboardManagerHeadPatch = ParseBool(it->second, _testKeyboardManagerHeadPatch);
            }
            if (auto it = values.find("test_keyboard_accept_dump_route"); it != values.end()) {
                _testKeyboardAcceptDumpRoute = ParseBool(it->second, _testKeyboardAcceptDumpRoute);
            }
            if (auto it = values.find("use_native_button_injector"); it != values.end()) {
                _useNativeButtonInjector = ParseBool(it->second, _useNativeButtonInjector);
            }
            if (auto it = values.find("use_native_frame_injector"); it != values.end()) {
                _useNativeFrameInjector = ParseBool(it->second, _useNativeFrameInjector);
            }
            if (auto it = values.find("native_button_hook_mode"); it != values.end()) {
                _nativeButtonHookMode = ParseNativeButtonHookMode(it->second, _nativeButtonHookMode);
            }
        };

        try {
            if (auto it = sections.find("Logging"); it != sections.end()) {
                parseLogging(it->second);
            }
            if (auto it = sections.find("Injection"); it != sections.end()) {
                parseInjection(it->second);
            }
        }
        catch (const std::exception& e) {
            logger::warn("[DualPad][RuntimeConfig] Parse error: {}", e.what());
        }

        logger::info(
            "[DualPad][RuntimeConfig] logging packets={} hex={} state={} mapping={} synthetic={} actionPlan={} native={} keyboard={} injection upstreamGamepad={} upstreamMode={} upstreamKeyboard={} upstreamKeyboardMode={} keyboardSourcePatchTest={} keyboardManagerHeadPatchTest={} keyboardAcceptDumpRouteTest={} nativeButton={} nativeButtonMode={} nativeFrame={}",
            _logInputPackets,
            _logInputHex,
            _logInputState,
            _logMappingEvents,
            _logSyntheticState,
            _logActionPlan,
            _logNativeInjection,
            _logKeyboardInjection,
            _useUpstreamGamepadHook,
            ToString(_upstreamGamepadHookMode),
            _useUpstreamKeyboardHook,
            ToString(_upstreamKeyboardHookMode),
            _testKeyboardEventSourcePatch,
            _testKeyboardManagerHeadPatch,
            _testKeyboardAcceptDumpRoute,
            _useNativeButtonInjector,
            ToString(_nativeButtonHookMode),
            _useNativeFrameInjector);
        if (_useUpstreamGamepadHook) {
            logger::warn(
                "[DualPad][RuntimeConfig] use_upstream_gamepad_hook enables the official upstream XInput route; rollback remains use_upstream_gamepad_hook=false (mode={})",
                ToString(_upstreamGamepadHookMode));
        }
        if (_useUpstreamKeyboardHook) {
            logger::warn(
                "[DualPad][RuntimeConfig] use_upstream_keyboard_hook enables the keyboard experimental route (mode={}); rollback remains use_upstream_keyboard_hook=false",
                ToString(_upstreamKeyboardHookMode));
        }
        if (_testKeyboardEventSourcePatch) {
            logger::warn(
                "[DualPad][RuntimeConfig] test_keyboard_event_source_patch is enabled; dispatch-hook will overwrite event+0x18 with the expected global source object for reverse-engineering validation");
        }
        if (_testKeyboardManagerHeadPatch) {
            logger::warn(
                "[DualPad][RuntimeConfig] test_keyboard_manager_head_patch is enabled; dispatch-hook will temporarily write qword_142F50B28+0x380/currentHead before sub_140C15E00 for reverse-engineering validation");
        }
        if (_testKeyboardAcceptDumpRoute) {
            logger::warn(
                "[DualPad][RuntimeConfig] test_keyboard_accept_dump_route is enabled; Menu.Confirm will temporarily prefer KeyboardNative so native/synthetic Accept templates can be captured");
        }
        if (_useNativeButtonInjector) {
            logger::warn(
                "[DualPad][RuntimeConfig] use_native_button_injector enables the deprecated consumer-side native-button experiment; keep it off unless you are reproducing old PollInputDevices/ControlMap failures (mode={})",
                ToString(_nativeButtonHookMode));
        }
        if (_useNativeFrameInjector) {
            logger::warn(
                "[DualPad][RuntimeConfig] use_native_frame_injector is experimental while thumbsticks still use BSInputEventQueue fallback");
        }
        return true;
    }

    void RuntimeConfig::ResetToDefaults()
    {
        _logInputPackets = false;
        _logInputHex = false;
        _logInputState = false;
        _logMappingEvents = false;
        _logSyntheticState = false;
        _logActionPlan = false;
        _logNativeInjection = false;
        _logKeyboardInjection = false;

        _useUpstreamGamepadHook = true;
        _upstreamGamepadHookMode = UpstreamGamepadHookMode::PollXInputCall;
        _useUpstreamKeyboardHook = false;
        _upstreamKeyboardHookMode = UpstreamKeyboardHookMode::SemanticMid;
        _testKeyboardEventSourcePatch = false;
        _testKeyboardManagerHeadPatch = false;
        _testKeyboardAcceptDumpRoute = false;
        _useNativeButtonInjector = false;
        _useNativeFrameInjector = false;
        _nativeButtonHookMode = NativeButtonHookMode::DropProbe;
    }
}
