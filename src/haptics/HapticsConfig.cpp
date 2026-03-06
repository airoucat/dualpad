#include "pch.h"
#include "haptics/HapticsConfig.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>

namespace logger = SKSE::log;

namespace dualpad::haptics
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

        inline bool ParseBool(const std::string& s, bool defaultValue = false)
        {
            const auto v = ToLower(Trim(s));
            if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
            if (v == "0" || v == "false" || v == "no" || v == "off") return false;
            return defaultValue;
        }

        inline float ParseFloat(
            const std::unordered_map<std::string, std::string>& values,
            const char* key,
            float current)
        {
            auto it = values.find(key);
            return (it != values.end()) ? std::stof(it->second) : current;
        }

        inline std::uint32_t ParseUInt(
            const std::unordered_map<std::string, std::string>& values,
            const char* key,
            std::uint32_t current)
        {
            auto it = values.find(key);
            return (it != values.end()) ? static_cast<std::uint32_t>(std::stoul(it->second)) : current;
        }
    }

    HapticsConfig& HapticsConfig::GetSingleton()
    {
        static HapticsConfig instance;
        return instance;
    }

    std::filesystem::path HapticsConfig::GetDefaultPath()
    {
        return "Data/SKSE/Plugins/DualPadHaptics.ini";
    }

    bool HapticsConfig::Load(const std::filesystem::path& path)
    {
        const auto cfgPath = path.empty() ? GetDefaultPath() : path;
        logger::info("[Haptics][Config] Loading from: {}", cfgPath.string());

        if (!std::filesystem::exists(cfgPath)) {
            logger::warn("[Haptics][Config] file not found, using defaults");
            return false;
        }

        return ParseIniFile(cfgPath);
    }

    bool HapticsConfig::ParseIniFile(const std::filesystem::path& path)
    {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            logger::error("[Haptics][Config] failed to open: {}", path.string());
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
            auto val = Trim(line.substr(eqPos + 1));
            if (key.empty()) {
                continue;
            }

            sections[section][key] = val;
        }

        for (const auto& [sec, kv] : sections) {
            try {
                if (sec == "Core") {
                    if (kv.count("enabled")) enabled = ParseBool(kv.at("enabled"), enabled);
                }
                else if (sec == "NativeVibration") {
                    leftMotorScale = ParseFloat(kv, "left_motor_scale", leftMotorScale);
                    rightMotorScale = ParseFloat(kv, "right_motor_scale", rightMotorScale);
                    maxIntensity = ParseFloat(kv, "max_intensity", maxIntensity);
                    deadzone = ParseFloat(kv, "deadzone", deadzone);
                }
                else if (sec == "Mixer") {
                    maxIntensity = ParseFloat(kv, "limiter", maxIntensity);
                    deadzone = ParseFloat(kv, "deadzone", deadzone);
                }
                else if (sec == "Debug") {
                    statsIntervalMs = ParseUInt(kv, "stats_interval_ms", statsIntervalMs);
                    if (kv.count("log_native_vibration")) {
                        logNativeVibration = ParseBool(kv.at("log_native_vibration"), logNativeVibration);
                    }
                }
            }
            catch (const std::exception& e) {
                logger::warn("[Haptics][Config] parse error [{}]: {}", sec, e.what());
            }
        }

        leftMotorScale = std::max(leftMotorScale, 0.0f);
        rightMotorScale = std::max(rightMotorScale, 0.0f);
        maxIntensity = std::clamp(maxIntensity, 0.0f, 1.0f);
        deadzone = std::clamp(deadzone, 0.0f, 1.0f);

        logger::info(
            "[Haptics][Config] Native vibration config loaded enabled={} leftScale={:.2f} rightScale={:.2f} max={:.2f} deadzone={:.2f}",
            enabled,
            leftMotorScale,
            rightMotorScale,
            maxIntensity,
            deadzone);
        return true;
    }
}
