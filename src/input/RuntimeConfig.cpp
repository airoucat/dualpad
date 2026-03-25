#include "pch.h"
#include "input/RuntimeConfig.h"

#include "input/IniParseHelpers.h"

#include <SKSE/SKSE.h>
#include <fstream>
#include <string>
#include <unordered_map>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        inline UpstreamGamepadHookMode ParseUpstreamGamepadHookMode(
            const std::string& value,
            UpstreamGamepadHookMode defaultValue)
        {
            const auto normalized = ini::ToLower(ini::Trim(value));
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

            if (lineNo == 1) {
                ini::StripUtf8Bom(line);
            }

            line = ini::Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                section = ini::Trim(line.substr(1, line.size() - 2));
                continue;
            }

            const auto eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                continue;
            }

            auto key = ini::Trim(line.substr(0, eqPos));
            auto value = ini::Trim(line.substr(eqPos + 1));
            if (key.empty()) {
                continue;
            }

            sections[section][key] = value;
        }

        const auto parseLogging = [&](const auto& values) {
            if (auto it = values.find("log_input_packets"); it != values.end()) {
                _logInputPackets = ini::ParseBool(it->second, _logInputPackets);
            }
            if (auto it = values.find("log_input_hex"); it != values.end()) {
                _logInputHex = ini::ParseBool(it->second, _logInputHex);
            }
            if (auto it = values.find("log_input_state"); it != values.end()) {
                _logInputState = ini::ParseBool(it->second, _logInputState);
            }
            if (auto it = values.find("log_mapping_events"); it != values.end()) {
                _logMappingEvents = ini::ParseBool(it->second, _logMappingEvents);
            }
            if (auto it = values.find("log_synthetic_state"); it != values.end()) {
                _logSyntheticState = ini::ParseBool(it->second, _logSyntheticState);
            }
            if (auto it = values.find("log_action_plan"); it != values.end()) {
                _logActionPlan = ini::ParseBool(it->second, _logActionPlan);
            }
            if (auto it = values.find("log_native_injection"); it != values.end()) {
                _logNativeInjection = ini::ParseBool(it->second, _logNativeInjection);
            }
            if (auto it = values.find("log_keyboard_injection"); it != values.end()) {
                _logKeyboardInjection = ini::ParseBool(it->second, _logKeyboardInjection);
            }
        };

        const auto parseInjection = [&](const auto& values) {
            if (auto it = values.find("use_upstream_gamepad_hook"); it != values.end()) {
                _useUpstreamGamepadHook = ini::ParseBool(it->second, _useUpstreamGamepadHook);
            }
            if (auto it = values.find("upstream_gamepad_hook_mode"); it != values.end()) {
                const auto normalized = ini::ToLower(ini::Trim(it->second));
                if (normalized == "poll-vtable" || normalized == "poll_vtable" || normalized == "vtable") {
                    logger::warn(
                        "[DualPad][RuntimeConfig] upstream_gamepad_hook_mode='{}' is retired; using poll-xinput-call",
                        it->second);
                }
                _upstreamGamepadHookMode = ParseUpstreamGamepadHookMode(it->second, _upstreamGamepadHookMode);
            }
        };

        const auto parseFeatures = [&](const auto& values) {
            if (auto it = values.find("enable_combo_native_hotkeys3_to_8"); it != values.end()) {
                _enableComboNativeHotkeys3To8 =
                    ini::ParseBool(it->second, _enableComboNativeHotkeys3To8);
            }
        };

        try {
            if (auto it = sections.find("Logging"); it != sections.end()) {
                parseLogging(it->second);
            }
            if (auto it = sections.find("Injection"); it != sections.end()) {
                parseInjection(it->second);
            }
            if (auto it = sections.find("Features"); it != sections.end()) {
                parseFeatures(it->second);
            }
        }
        catch (const std::exception& e) {
            logger::warn("[DualPad][RuntimeConfig] Parse error: {}", e.what());
        }

        logger::info(
            "[DualPad][RuntimeConfig] logging packets={} hex={} state={} mapping={} synthetic={} actionPlan={} native={} keyboard={} injection upstreamGamepad={} upstreamMode={} features comboHotkeys3to8={}",
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
            _enableComboNativeHotkeys3To8);
        if (_useUpstreamGamepadHook) {
            logger::warn(
                "[DualPad][RuntimeConfig] use_upstream_gamepad_hook enables the official upstream XInput route; rollback remains use_upstream_gamepad_hook=false (mode={})",
                ToString(_upstreamGamepadHookMode));
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
        _enableComboNativeHotkeys3To8 = false;
    }
}
