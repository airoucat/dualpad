#include "pch.h"
#include "haptics/HapticsConfig.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
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

        inline const char* ModeToString(HapticsConfig::HapticsMode m)
        {
            switch (m) {
            case HapticsConfig::HapticsMode::NativeOnly: return "NativeOnly";
            case HapticsConfig::HapticsMode::Hybrid:     return "Hybrid(legacy)";
            case HapticsConfig::HapticsMode::AudioDriven:return "CustomAudio(AudioDriven)";
            default:                                     return "Unknown";
            }
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
            // 默认模式下也输出统一语义日志
            logger::info("[Haptics][Config] Mode={}", ModeToString(hapticsMode));
            return false;
        }

        const bool ok = ParseIniFile(cfgPath);
        logger::info("[Haptics][Config] Final Mode={}", ModeToString(hapticsMode));
        return ok;
    }

    bool HapticsConfig::ParseIniFile(const std::filesystem::path& path)
    {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            logger::error("[Haptics][Config] failed to open: {}", path.string());
            return false;
        }

        // 防止热重载累加
        eventConfigs.clear();
        duckingRules.clear();

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
                    if (kv.count("tick_ms")) tickMs = std::stoul(kv.at("tick_ms"));
                    if (kv.count("use_qpc")) useQpc = ParseBool(kv.at("use_qpc"), useQpc);
                    if (kv.count("hot_reload")) hotReload = ParseBool(kv.at("hot_reload"), hotReload);
                }
                else if (sec == "AudioTap") {
                    if (kv.count("block_prefer_samples")) blockPreferSamples = std::stoul(kv.at("block_prefer_samples"));
                    if (kv.count("queue_capacity")) queueCapacity = std::stoul(kv.at("queue_capacity"));
                }
                else if (sec == "Scoring") {
                    if (kv.count("window_pre_ms")) windowPreMs = std::stoul(kv.at("window_pre_ms"));
                    if (kv.count("window_post_ms")) windowPostMs = std::stoul(kv.at("window_post_ms"));
                    if (kv.count("min_confidence")) minConfidence = std::stof(kv.at("min_confidence"));
                    if (kv.count("weight_timing")) weightTiming = std::stof(kv.at("weight_timing"));
                    if (kv.count("weight_attack")) weightAttack = std::stof(kv.at("weight_attack"));
                    if (kv.count("weight_spectrum")) weightSpectrum = std::stof(kv.at("weight_spectrum"));
                    if (kv.count("weight_duration")) weightDuration = std::stof(kv.at("weight_duration"));
                    if (kv.count("weight_channel")) weightChannel = std::stof(kv.at("weight_channel"));
                    if (kv.count("weight_meta")) weightMeta = std::stof(kv.at("weight_meta"));
                }
                else if (sec == "Mixer") {
                    if (kv.count("limiter")) limiter = std::stof(kv.at("limiter"));
                    if (kv.count("compressor_attack_ms")) compressorAttackMs = std::stoul(kv.at("compressor_attack_ms"));
                    if (kv.count("compressor_release_ms")) compressorReleaseMs = std::stoul(kv.at("compressor_release_ms"));
                    if (kv.count("slew_per_tick")) slewPerTick = std::stof(kv.at("slew_per_tick"));
                    if (kv.count("deadzone")) deadzone = std::stof(kv.at("deadzone"));
                }
                else if (sec == "Ducking") {
                    if (kv.count("hit_duck_footstep")) hitDuckFootstep = std::stof(kv.at("hit_duck_footstep"));
                    if (kv.count("hit_duck_ambient")) hitDuckAmbient = std::stof(kv.at("hit_duck_ambient"));
                    if (kv.count("spell_duck_footstep")) spellDuckFootstep = std::stof(kv.at("spell_duck_footstep"));
                }
                else if (sec == "Priority") {
                    if (kv.count("hit")) priorityHit = std::stoi(kv.at("hit"));
                    if (kv.count("spell")) prioritySpell = std::stoi(kv.at("spell"));
                    if (kv.count("swing")) prioritySwing = std::stoi(kv.at("swing"));
                    if (kv.count("footstep")) priorityFootstep = std::stoi(kv.at("footstep"));
                    if (kv.count("ambient")) priorityAmbient = std::stoi(kv.at("ambient"));
                }
                else if (sec == "Fallback") {
                    if (kv.count("enable_base_pulse")) enableBasePulse = ParseBool(kv.at("enable_base_pulse"), enableBasePulse);
                    if (kv.count("base_pulse_hit")) basePulseHit = std::stof(kv.at("base_pulse_hit"));
                    if (kv.count("base_pulse_swing")) basePulseSwing = std::stof(kv.at("base_pulse_swing"));
                    if (kv.count("base_pulse_footstep")) basePulseFootstep = std::stof(kv.at("base_pulse_footstep"));

                    if (kv.count("audio_driven_prefer_audio_only")) {
                        audioDrivenPreferAudioOnly = ParseBool(kv.at("audio_driven_prefer_audio_only"), audioDrivenPreferAudioOnly);
                    }
                    if (kv.count("fallback_base_when_no_match")) {
                        fallbackBaseWhenNoMatch = ParseBool(kv.at("fallback_base_when_no_match"), fallbackBaseWhenNoMatch);
                    }
                }
                else if (sec == "Device") {
                    if (kv.count("output_backend")) outputBackend = kv.at("output_backend");
                    if (kv.count("retry_count")) retryCount = std::stoul(kv.at("retry_count"));
                    if (kv.count("reconnect_ms")) reconnectMs = std::stoul(kv.at("reconnect_ms"));
                }
                else if (sec == "Debug") {
                    if (kv.count("log_level")) logLevel = kv.at("log_level");
                    if (kv.count("stats_interval_ms")) statsIntervalMs = std::stoul(kv.at("stats_interval_ms"));
                    if (kv.count("dump_metrics")) dumpMetrics = ParseBool(kv.at("dump_metrics"), dumpMetrics);
                }
                else if (sec == "LowLatency") {
                    LoadLowLatencyConfig(kv);
                }
                else if (sec == "Mode") {
                    LoadModeConfig(kv);
                }
                else if (sec == "EventPriority") {
                    LoadEventPriorityConfig(kv);
                }
                else if (sec == "DuckingMatrix") {
                    LoadDuckingMatrix(kv);
                }
                else if (sec == "ExtensionAPI") {
                    LoadExtensionConfig(kv);
                }
            }
            catch (const std::exception& e) {
                logger::warn("[Haptics][Config] parse error [{}]: {}", sec, e.what());
            }
        }

        logger::info("[Haptics][Config] Config loaded successfully");
        return true;
    }

    void HapticsConfig::LoadModeConfig(const std::unordered_map<std::string, std::string>& values)
    {
        if (values.count("haptics_mode")) {
            const auto mode = ToLower(values.at("haptics_mode"));

            if (mode == "nativeonly" || mode == "native_only") {
                hapticsMode = HapticsMode::NativeOnly;
            }
            else if (mode == "customaudio" || mode == "custom_audio" || mode == "audiodriven" || mode == "audio_driven") {
                hapticsMode = HapticsMode::AudioDriven;
            }
            else if (mode == "hybrid") {
                // 兼容旧配置：当前精简阶段统一映射为 CustomAudio
                hapticsMode = HapticsMode::AudioDriven;
                logger::warn("[Haptics][Config] haptics_mode=hybrid is legacy, remapped to CustomAudio(AudioDriven)");
            }
            else {
                logger::warn("[Haptics][Config] unknown haptics_mode='{}', fallback to CustomAudio(AudioDriven)", mode);
                hapticsMode = HapticsMode::AudioDriven;
            }
        }

        if (values.count("allow_subtle_events_without_audio")) {
            allowSubtleEventsWithoutAudio = ParseBool(values.at("allow_subtle_events_without_audio"), allowSubtleEventsWithoutAudio);
        }

        if (values.count("audio_driven_prefer_audio_only")) {
            audioDrivenPreferAudioOnly = ParseBool(values.at("audio_driven_prefer_audio_only"), audioDrivenPreferAudioOnly);
        }

        if (values.count("fallback_base_when_no_match")) {
            fallbackBaseWhenNoMatch = ParseBool(values.at("fallback_base_when_no_match"), fallbackBaseWhenNoMatch);
        }

        logger::info("[Haptics][Config] Mode={} subtleNoAudio={} preferAudioOnly={} fallbackNoMatch={}",
            ModeToString(hapticsMode),
            allowSubtleEventsWithoutAudio,
            audioDrivenPreferAudioOnly,
            fallbackBaseWhenNoMatch);
    }

    void HapticsConfig::LoadEventPriorityConfig(const std::unordered_map<std::string, std::string>& values)
    {
        for (const auto& [name, v] : values) {
            EventType type = StringToEventType(name);
            if (type == EventType::Unknown) {
                continue;
            }

            EventConfig cfg{};
            int allowDuck = 1;
            int reqAudio = 0;

            // 格式: priority, ttl_ms, focus_window_ms, ducking_strength, allow_ducking, requires_audio
            if (std::sscanf(v.c_str(), "%hhu, %u, %u, %f, %d, %d",
                &cfg.priority, &cfg.ttlMs, &cfg.focusWindowMs, &cfg.duckingStrength, &allowDuck, &reqAudio) == 6) {
                cfg.allowDucking = (allowDuck != 0);
                cfg.requiresAudio = (reqAudio != 0);
                eventConfigs[type] = cfg;

                logger::info("[Haptics][Config] Event {}: pri={} ttl={}ms focus={}ms duck={:.2f} reqAudio={}",
                    name, cfg.priority, cfg.ttlMs, cfg.focusWindowMs, cfg.duckingStrength, cfg.requiresAudio);
            }
        }
    }

    void HapticsConfig::LoadDuckingMatrix(const std::unordered_map<std::string, std::string>& values)
    {
        for (const auto& [k, v] : values) {
            const auto arrow = k.find("->");
            if (arrow == std::string::npos) {
                continue;
            }

            std::string focusStr = Trim(k.substr(0, arrow));
            std::string targetStr = Trim(k.substr(arrow + 2));

            EventType focusType = StringToEventType(ToLower(focusStr));
            EventType targetType = StringToEventType(ToLower(targetStr));
            if (focusType == EventType::Unknown || targetType == EventType::Unknown) {
                continue;
            }

            DuckingRule rule{};
            rule.focusType = focusType;
            rule.targetType = targetType;
            rule.duckFactor = std::clamp(std::stof(v), 0.0f, 1.0f);

            duckingRules.push_back(rule);

            logger::info("[Haptics][Config] Ducking: {} -> {} = {:.2f}",
                focusStr, targetStr, rule.duckFactor);
        }
    }

    void HapticsConfig::LoadLowLatencyConfig(const std::unordered_map<std::string, std::string>& values)
    {
        if (values.count("correction_window_ms")) correctionWindowMs = std::stoul(values.at("correction_window_ms"));
        if (values.count("event_short_window_ms")) eventShortWindowMs = std::stoul(values.at("event_short_window_ms"));
        if (values.count("submit_feature_cache_ms")) submitFeatureCacheMs = std::stoul(values.at("submit_feature_cache_ms"));
        if (values.count("correction_min_score")) correctionMinScore = std::stof(values.at("correction_min_score"));
        if (values.count("immediate_gain")) immediateGain = std::stof(values.at("immediate_gain"));
        if (values.count("correction_gain")) correctionGain = std::stof(values.at("correction_gain"));
        if (values.count("enable_ambient_passthrough")) enableAmbientPassthrough = ParseBool(values.at("enable_ambient_passthrough"), enableAmbientPassthrough);
    }

    void HapticsConfig::LoadExtensionConfig(const std::unordered_map<std::string, std::string>& values)
    {
        if (values.count("enable_custom_events")) {
            enableCustomEvents = ParseBool(values.at("enable_custom_events"), enableCustomEvents);
        }
        if (values.count("max_custom_events")) {
            maxCustomEvents = std::stoul(values.at("max_custom_events"));
        }
        if (values.count("custom_event_priority_base")) {
            customEventPriorityBase = static_cast<std::uint8_t>(std::stoul(values.at("custom_event_priority_base")));
        }

        logger::info("[Haptics][Config] Extension API: enabled={} maxCustom={} basePri={}",
            enableCustomEvents, maxCustomEvents, customEventPriorityBase);
    }

    bool HapticsConfig::IsEventAllowed(EventType type) const
    {
        // NativeOnly 模式下，本插件不应生成任何自定义事件输出
        if (IsNativeOnly()) {
            return false;
        }

        auto it = eventConfigs.find(type);
        if (it == eventConfigs.end()) {
            return true;
        }

        const auto& ec = it->second;
        if (!ec.requiresAudio) {
            return true;
        }

        // requiresAudio=true 时，当前为 CustomAudio 模式 -> 允许
        if (IsCustomAudioMode()) {
            return true;
        }

        return allowSubtleEventsWithoutAudio;
    }

    const HapticsConfig::EventConfig* HapticsConfig::GetEventConfig(EventType type) const
    {
        auto it = eventConfigs.find(type);
        return (it != eventConfigs.end()) ? &it->second : nullptr;
    }

    EventType HapticsConfig::StringToEventType(const std::string& str) const
    {
        static const std::unordered_map<std::string, EventType> map = {
            {"hit_impact", EventType::HitImpact},
            {"block", EventType::Block},
            {"weapon_swing", EventType::WeaponSwing},
            {"spell_impact", EventType::SpellImpact},
            {"spell_cast", EventType::SpellCast},
            {"land", EventType::Land},
            {"jump", EventType::Jump},
            {"footstep", EventType::Footstep},
            {"bow_release", EventType::BowRelease},
            {"shout", EventType::Shout}
        };

        auto key = ToLower(str);
        auto it = map.find(key);
        return (it != map.end()) ? it->second : EventType::Unknown;
    }
}