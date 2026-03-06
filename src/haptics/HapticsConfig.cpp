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

        inline const char* MixerModeToString(HapticsConfig::MixerSameGroupMode mode)
        {
            switch (mode) {
            case HapticsConfig::MixerSameGroupMode::Avg: return "avg";
            case HapticsConfig::MixerSameGroupMode::Max: return "max";
            default:                                     return "max";
            }
        }

        inline HapticsConfig::MixerSameGroupMode ParseMixerMode(
            const std::string& value,
            HapticsConfig::MixerSameGroupMode fallback)
        {
            const auto mode = ToLower(Trim(value));
            if (mode == "avg") {
                return HapticsConfig::MixerSameGroupMode::Avg;
            }
            if (mode == "max") {
                return HapticsConfig::MixerSameGroupMode::Max;
            }
            return fallback;
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
                else if (sec == "SemanticCache" || sec == "Semantic") {
                    LoadSemanticConfig(kv);
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
                else if (sec == "DynamicPool") {
                    LoadDynamicPoolConfig(kv);
                }
                else if (sec == "ExtensionAPI") {
                    LoadExtensionConfig(kv);
                }
                else if (sec == "Gate") {
                    LoadGateConfig(kv);
                }
                else if (sec == "Budget") {
                    LoadBudgetConfig(kv);
                }
                else if (sec == "HidTx" || sec == "OutputScheduler") {
                    LoadHidTxConfig(kv);
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

            if (::sscanf_s(v.c_str(), "%hhu, %u, %u, %f, %d, %d",
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
        if (values.count("allow_unknown_audio_event")) allowUnknownAudioEvent = ParseBool(values.at("allow_unknown_audio_event"), allowUnknownAudioEvent);
        if (values.count("enable_unknown_semantic_gate")) enableUnknownSemanticGate = ParseBool(values.at("enable_unknown_semantic_gate"), enableUnknownSemanticGate);
        if (values.count("allow_unknown_footstep")) allowUnknownFootstep = ParseBool(values.at("allow_unknown_footstep"), allowUnknownFootstep);
        if (values.count("unknown_min_input_level")) unknownMinInputLevel = std::clamp(std::stof(values.at("unknown_min_input_level")), 0.0f, 1.0f);
        if (values.count("unknown_semantic_min_confidence")) unknownSemanticMinConfidence = std::clamp(std::stof(values.at("unknown_semantic_min_confidence")), 0.0f, 1.0f);
        if (values.count("allow_background_event")) allowBackgroundEvent = ParseBool(values.at("allow_background_event"), allowBackgroundEvent);
        if (values.count("enable_unknown_promotion")) enableUnknownPromotion = ParseBool(values.at("enable_unknown_promotion"), enableUnknownPromotion);
        if (values.count("unknown_promotion_min_confidence")) {
            unknownPromotionMinConfidence = std::clamp(std::stof(values.at("unknown_promotion_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("relative_energy_ratio_threshold")) {
            relativeEnergyRatioThreshold = std::clamp(std::stof(values.at("relative_energy_ratio_threshold")), 1.0f, 8.0f);
        }
        if (values.count("refractory_hit_ms")) refractoryHitMs = std::stoul(values.at("refractory_hit_ms"));
        if (values.count("refractory_swing_ms")) refractorySwingMs = std::stoul(values.at("refractory_swing_ms"));
        if (values.count("refractory_footstep_ms")) refractoryFootstepMs = std::stoul(values.at("refractory_footstep_ms"));
        if (values.count("trace_binding_ttl_ms")) traceBindingTtlMs = std::stoul(values.at("trace_binding_ttl_ms"));
        if (values.count("enable_audio_lock_binding")) {
            enableAudioLockBinding = ParseBool(values.at("enable_audio_lock_binding"), enableAudioLockBinding);
        }
        if (values.count("audio_lock_start_min_confidence")) {
            audioLockStartMinConfidence = std::clamp(
                std::stof(values.at("audio_lock_start_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("audio_lock_unknown_start_min_confidence")) {
            audioLockUnknownStartMinConfidence = std::clamp(
                std::stof(values.at("audio_lock_unknown_start_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("audio_lock_extend_min_confidence")) {
            audioLockExtendMinConfidence = std::clamp(
                std::stof(values.at("audio_lock_extend_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("audio_lock_extend_grace_ms")) {
            audioLockExtendGraceMs = std::max<std::uint32_t>(
                0u, std::stoul(values.at("audio_lock_extend_grace_ms")));
        }

        if (values.count("hid_tx_fg_capacity")) {
            hidTxFgCapacity = std::max<std::uint32_t>(8u, std::stoul(values.at("hid_tx_fg_capacity")));
        }
        if (values.count("hid_tx_bg_capacity")) {
            hidTxBgCapacity = std::max<std::uint32_t>(8u, std::stoul(values.at("hid_tx_bg_capacity")));
        }
        if (values.count("hid_stale_us")) {
            hidStaleUs = std::max<std::uint32_t>(1000u, std::stoul(values.at("hid_stale_us")));
        }
        if (values.count("hid_merge_window_fg_us")) {
            hidMergeWindowFgUs = std::max<std::uint32_t>(0u, std::stoul(values.at("hid_merge_window_fg_us")));
        }
        if (values.count("hid_merge_window_bg_us")) {
            hidMergeWindowBgUs = std::max<std::uint32_t>(0u, std::stoul(values.at("hid_merge_window_bg_us")));
        }
        if (values.count("hid_scheduler_lookahead_us")) {
            hidSchedulerLookaheadUs = std::max<std::uint32_t>(0u, std::stoul(values.at("hid_scheduler_lookahead_us")));
        }
        if (values.count("hid_bg_budget")) {
            hidBgBudget = std::max<std::uint32_t>(1u, std::stoul(values.at("hid_bg_budget")));
        }
        if (values.count("hid_fg_preempt")) {
            hidFgPreempt = ParseBool(values.at("hid_fg_preempt"), hidFgPreempt);
        }
        if (values.count("hid_max_send_per_flush")) {
            hidMaxSendPerFlush = std::clamp<std::uint32_t>(
                std::stoul(values.at("hid_max_send_per_flush")), 1u, 8u);
        }
        if (values.count("hid_min_repeat_interval_us")) {
            hidMinRepeatIntervalUs = std::max<std::uint32_t>(
                1000u, std::stoul(values.at("hid_min_repeat_interval_us")));
        }
        if (values.count("hid_idle_repeat_interval_us")) {
            hidIdleRepeatIntervalUs = std::max<std::uint32_t>(
                2000u, std::stoul(values.at("hid_idle_repeat_interval_us")));
        }
        if (values.count("hid_stop_clears_queue")) {
            hidStopClearsQueue = ParseBool(values.at("hid_stop_clears_queue"), hidStopClearsQueue);
        }
        if (values.count("enable_state_track_scheduler")) {
            enableStateTrackScheduler = ParseBool(
                values.at("enable_state_track_scheduler"), enableStateTrackScheduler);
        }
        if (values.count("enable_state_track_impact_renderer")) {
            enableStateTrackImpactRenderer = ParseBool(
                values.at("enable_state_track_impact_renderer"), enableStateTrackImpactRenderer);
        }
        if (values.count("enable_state_track_swing_renderer")) {
            enableStateTrackSwingRenderer = ParseBool(
                values.at("enable_state_track_swing_renderer"), enableStateTrackSwingRenderer);
        }
        if (values.count("enable_state_track_footstep_renderer")) {
            enableStateTrackFootstepRenderer = ParseBool(
                values.at("enable_state_track_footstep_renderer"), enableStateTrackFootstepRenderer);
        }
        if (values.count("enable_state_track_footstep_token_renderer")) {
            enableStateTrackFootstepTokenRenderer = ParseBool(
                values.at("enable_state_track_footstep_token_renderer"), enableStateTrackFootstepTokenRenderer);
        }
        if (values.count("enable_state_track_footstep_truth_trigger")) {
            enableStateTrackFootstepTruthTrigger = ParseBool(
                values.at("enable_state_track_footstep_truth_trigger"), enableStateTrackFootstepTruthTrigger);
        }
        if (values.count("enable_state_track_footstep_context_gate")) {
            enableStateTrackFootstepContextGate = ParseBool(
                values.at("enable_state_track_footstep_context_gate"), enableStateTrackFootstepContextGate);
        }
        if (values.count("enable_state_track_footstep_motion_gate")) {
            enableStateTrackFootstepMotionGate = ParseBool(
                values.at("enable_state_track_footstep_motion_gate"), enableStateTrackFootstepMotionGate);
        }
        if (values.count("state_track_footstep_recent_move_ms")) {
            stateTrackFootstepRecentMoveMs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_footstep_recent_move_ms")), 0u, 5000u);
        }
        if (values.count("enable_footstep_audio_matcher_shadow")) {
            enableFootstepAudioMatcherShadow = ParseBool(
                values.at("enable_footstep_audio_matcher_shadow"), enableFootstepAudioMatcherShadow);
        }
        if (values.count("footstep_audio_matcher_lookback_us")) {
            footstepAudioMatcherLookbackUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_audio_matcher_lookback_us")), 0u, 120000u);
        }
        if (values.count("footstep_audio_matcher_lookahead_us")) {
            footstepAudioMatcherLookaheadUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_audio_matcher_lookahead_us")), 0u, 120000u);
        }
        if (values.count("footstep_audio_matcher_max_candidates")) {
            footstepAudioMatcherMaxCandidates = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_audio_matcher_max_candidates")), 4u, 512u);
        }
        if (values.count("footstep_audio_matcher_min_score")) {
            footstepAudioMatcherMinScore = std::clamp(
                std::stof(values.at("footstep_audio_matcher_min_score")), 0.0f, 1.0f);
        }
        if (values.count("enable_footstep_truth_bridge_shadow")) {
            enableFootstepTruthBridgeShadow = ParseBool(
                values.at("enable_footstep_truth_bridge_shadow"), enableFootstepTruthBridgeShadow);
        }
        if (values.count("footstep_truth_bridge_lookback_us")) {
            footstepTruthBridgeLookbackUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_truth_bridge_lookback_us")), 1000u, 600000u);
        }
        if (values.count("footstep_truth_bridge_lookahead_us")) {
            footstepTruthBridgeLookaheadUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_truth_bridge_lookahead_us")), 1000u, 800000u);
        }
        if (values.count("footstep_truth_bridge_binding_ttl_ms")) {
            footstepTruthBridgeBindingTtlMs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_truth_bridge_binding_ttl_ms")), 100u, 10000u);
        }
        if (values.count("state_track_lookahead_min_us")) {
            stateTrackLookaheadMinUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_lookahead_min_us")), 800u, 12000u);
        }
        if (values.count("state_track_repeat_keep_max_overdue_us")) {
            stateTrackRepeatKeepMaxOverdueUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_repeat_keep_max_overdue_us")), 0u, 20000u);
        }
        if (values.count("state_track_release_hit_us")) {
            stateTrackReleaseHitUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_release_hit_us")), 12000u, 300000u);
        }
        if (values.count("state_track_release_swing_us")) {
            stateTrackReleaseSwingUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_release_swing_us")), 12000u, 300000u);
        }
        if (values.count("state_track_release_footstep_us")) {
            stateTrackReleaseFootstepUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_release_footstep_us")), 8000u, 300000u);
        }
        if (values.count("state_track_release_utility_us")) {
            stateTrackReleaseUtilityUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_release_utility_us")), 8000u, 300000u);
        }
    }

    void HapticsConfig::LoadDynamicPoolConfig(const std::unordered_map<std::string, std::string>& values)
    {
        if (values.count("enable_dynamic_haptic_pool")) {
            enableDynamicHapticPool = ParseBool(values.at("enable_dynamic_haptic_pool"), enableDynamicHapticPool);
        }
        if (values.count("dynamic_pool_top_k")) {
            dynamicPoolTopK = std::max<std::uint32_t>(1, std::stoul(values.at("dynamic_pool_top_k")));
        }
        if (values.count("dynamic_pool_min_confidence")) {
            dynamicPoolMinConfidence = std::clamp(
                std::stof(values.at("dynamic_pool_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("dynamic_pool_output_cap")) {
            dynamicPoolOutputCap = std::clamp(
                std::stof(values.at("dynamic_pool_output_cap")), 0.0f, 1.0f);
        }
        if (values.count("enable_dynamic_pool_shadow_probe")) {
            enableDynamicPoolShadowProbe = ParseBool(
                values.at("enable_dynamic_pool_shadow_probe"), enableDynamicPoolShadowProbe);
        }
        if (values.count("enable_dynamic_pool_learn_from_l2")) {
            enableDynamicPoolLearnFromL2 = ParseBool(
                values.at("enable_dynamic_pool_learn_from_l2"), enableDynamicPoolLearnFromL2);
        }
        if (values.count("dynamic_pool_l2_min_confidence")) {
            dynamicPoolL2MinConfidence = std::clamp(
                std::stof(values.at("dynamic_pool_l2_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("dynamic_pool_resolve_min_hits")) {
            dynamicPoolResolveMinHits = std::max<std::uint32_t>(
                1, std::stoul(values.at("dynamic_pool_resolve_min_hits")));
        }
        if (values.count("dynamic_pool_resolve_min_input_energy")) {
            dynamicPoolResolveMinInputEnergy = std::clamp(
                std::stof(values.at("dynamic_pool_resolve_min_input_energy")), 0.0f, 1.0f);
        }

        logger::info(
            "[Haptics][Config] DynamicPool enabled={} topK={} minConf={:.2f} outputCap={:.2f} shadowProbe={} learnL2={} l2Min={:.2f} resolveMinHits={} resolveMinInput={:.2f}",
            enableDynamicHapticPool,
            dynamicPoolTopK,
            dynamicPoolMinConfidence,
            dynamicPoolOutputCap,
            enableDynamicPoolShadowProbe,
            enableDynamicPoolLearnFromL2,
            dynamicPoolL2MinConfidence,
            dynamicPoolResolveMinHits,
            dynamicPoolResolveMinInputEnergy);
    }

    void HapticsConfig::LoadSemanticConfig(const std::unordered_map<std::string, std::string>& values)
    {
        if (values.count("enable_form_semantic_cache")) {
            enableFormSemanticCache = ParseBool(values.at("enable_form_semantic_cache"), enableFormSemanticCache);
        }
        if (values.count("enable_l1_form_semantic")) {
            enableL1FormSemantic = ParseBool(values.at("enable_l1_form_semantic"), enableL1FormSemantic);
        }
        if (values.count("enable_l1_voice_trace")) {
            enableL1VoiceTrace = ParseBool(values.at("enable_l1_voice_trace"), enableL1VoiceTrace);
        }
        if (values.count("enable_submit_nocontext_fallback")) {
            enableSubmitNoContextFallback = ParseBool(
                values.at("enable_submit_nocontext_fallback"), enableSubmitNoContextFallback);
        }
        if (values.count("enable_submit_nocontext_deep_fallback")) {
            enableSubmitNoContextDeepFallback = ParseBool(
                values.at("enable_submit_nocontext_deep_fallback"), enableSubmitNoContextDeepFallback);
        }
        if (values.count("l1_form_semantic_min_confidence")) {
            l1FormSemanticMinConfidence = std::clamp(
                std::stof(values.at("l1_form_semantic_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("trace_preferred_event_min_confidence")) {
            tracePreferredEventMinConfidence = std::clamp(
                std::stof(values.at("trace_preferred_event_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("trace_background_event_min_confidence")) {
            traceBackgroundEventMinConfidence = std::clamp(
                std::stof(values.at("trace_background_event_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("trace_allow_background_event")) {
            traceAllowBackgroundEvent = ParseBool(
                values.at("trace_allow_background_event"), traceAllowBackgroundEvent);
        }
        if (values.count("submit_semantic_max_attempts")) {
            submitSemanticScanMaxAttempts = std::clamp<std::uint32_t>(
                std::stoul(values.at("submit_semantic_max_attempts")), 1u, 15u);
        }
        if (values.count("submit_semantic_retry_interval_ms")) {
            submitSemanticRetryIntervalMs = std::max<std::uint32_t>(
                1u, std::stoul(values.at("submit_semantic_retry_interval_ms")));
        }
        if (values.count("semantic_rules_path")) {
            semanticRulesPath = values.at("semantic_rules_path");
        }
        if (values.count("semantic_cache_path")) {
            semanticCachePath = values.at("semantic_cache_path");
        }
        if (values.count("semantic_force_rebuild")) {
            semanticForceRebuild = ParseBool(values.at("semantic_force_rebuild"), semanticForceRebuild);
        }

        logger::info(
            "[Haptics][Config] Semantic cache={} l1={} l1VoiceTrace={} noCtxFallback={} noCtxDeepFallback={} minConf={:.2f} traceEvtMin={:.2f} traceBgMin={:.2f} traceBgAllow={} unkGate={} unkFootstep={} unkMinIn={:.2f} unkSemMin={:.2f} submitRetry(max={} interval={}ms) rules={} cache={} forceRebuild={}",
            enableFormSemanticCache,
            enableL1FormSemantic,
            enableL1VoiceTrace,
            enableSubmitNoContextFallback,
            enableSubmitNoContextDeepFallback,
            l1FormSemanticMinConfidence,
            tracePreferredEventMinConfidence,
            traceBackgroundEventMinConfidence,
            traceAllowBackgroundEvent,
            enableUnknownSemanticGate,
            allowUnknownFootstep,
            unknownMinInputLevel,
            unknownSemanticMinConfidence,
            submitSemanticScanMaxAttempts,
            submitSemanticRetryIntervalMs,
            semanticRulesPath,
            semanticCachePath,
            semanticForceRebuild);
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

    void HapticsConfig::LoadGateConfig(const std::unordered_map<std::string, std::string>& values)
    {
        if (values.count("allow_unknown_audio_event")) {
            allowUnknownAudioEvent = ParseBool(values.at("allow_unknown_audio_event"), allowUnknownAudioEvent);
        }
        if (values.count("allow_background_event")) {
            allowBackgroundEvent = ParseBool(values.at("allow_background_event"), allowBackgroundEvent);
        }
        if (values.count("allow_unknown_footstep")) {
            allowUnknownFootstep = ParseBool(values.at("allow_unknown_footstep"), allowUnknownFootstep);
        }
        if (values.count("enable_unknown_semantic_gate")) {
            enableUnknownSemanticGate = ParseBool(values.at("enable_unknown_semantic_gate"), enableUnknownSemanticGate);
        }
        if (values.count("unknown_semantic_min_confidence")) {
            unknownSemanticMinConfidence = std::clamp(
                std::stof(values.at("unknown_semantic_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("unknown_min_input_level")) {
            unknownMinInputLevel = std::clamp(std::stof(values.at("unknown_min_input_level")), 0.0f, 1.0f);
        }
        if (values.count("enable_unknown_promotion")) {
            enableUnknownPromotion = ParseBool(values.at("enable_unknown_promotion"), enableUnknownPromotion);
        }
        if (values.count("unknown_promotion_min_confidence")) {
            unknownPromotionMinConfidence = std::clamp(
                std::stof(values.at("unknown_promotion_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("relative_energy_ratio_threshold")) {
            relativeEnergyRatioThreshold = std::clamp(
                std::stof(values.at("relative_energy_ratio_threshold")), 1.0f, 8.0f);
        }
        if (values.count("refractory_hit_ms")) {
            refractoryHitMs = std::stoul(values.at("refractory_hit_ms"));
        }
        if (values.count("refractory_swing_ms")) {
            refractorySwingMs = std::stoul(values.at("refractory_swing_ms"));
        }
        if (values.count("refractory_footstep_ms")) {
            refractoryFootstepMs = std::stoul(values.at("refractory_footstep_ms"));
        }
        if (values.count("enable_audio_lock_binding")) {
            enableAudioLockBinding = ParseBool(values.at("enable_audio_lock_binding"), enableAudioLockBinding);
        }
        if (values.count("audio_lock_start_min_confidence")) {
            audioLockStartMinConfidence = std::clamp(
                std::stof(values.at("audio_lock_start_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("audio_lock_unknown_start_min_confidence")) {
            audioLockUnknownStartMinConfidence = std::clamp(
                std::stof(values.at("audio_lock_unknown_start_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("audio_lock_extend_min_confidence")) {
            audioLockExtendMinConfidence = std::clamp(
                std::stof(values.at("audio_lock_extend_min_confidence")), 0.0f, 1.0f);
        }
        if (values.count("audio_lock_extend_grace_ms")) {
            audioLockExtendGraceMs = std::max<std::uint32_t>(
                0u, std::stoul(values.at("audio_lock_extend_grace_ms")));
        }

        logger::info(
            "[Haptics][Config] Gate unkAllow={} bgAllow={} unkFootstep={} unkSemGate={} unkSemMin={:.2f} unkMinIn={:.2f} unkPromotion={} unkPromMin={:.2f} relEnergyRatio={:.2f} refr(hit/swing/foot)={}/{}/{}ms audioLock(en={},start={:.2f},unkStart={:.2f},extend={:.2f},grace={}ms)",
            allowUnknownAudioEvent,
            allowBackgroundEvent,
            allowUnknownFootstep,
            enableUnknownSemanticGate,
            unknownSemanticMinConfidence,
            unknownMinInputLevel,
            enableUnknownPromotion,
            unknownPromotionMinConfidence,
            relativeEnergyRatioThreshold,
            refractoryHitMs,
            refractorySwingMs,
            refractoryFootstepMs,
            enableAudioLockBinding,
            audioLockStartMinConfidence,
            audioLockUnknownStartMinConfidence,
            audioLockExtendMinConfidence,
            audioLockExtendGraceMs);
    }

    void HapticsConfig::LoadBudgetConfig(const std::unordered_map<std::string, std::string>& values)
    {
        if (values.count("mixer_foreground_top_n")) {
            mixerForegroundTopN = std::max<std::uint32_t>(1u, std::stoul(values.at("mixer_foreground_top_n")));
        }
        if (values.count("mixer_background_budget")) {
            mixerBackgroundBudget = std::clamp(std::stof(values.at("mixer_background_budget")), 0.0f, 1.0f);
        }
        if (values.count("mixer_same_group_mode")) {
            mixerSameGroupMode = ParseMixerMode(values.at("mixer_same_group_mode"), mixerSameGroupMode);
        }

        logger::info(
            "[Haptics][Config] Budget fgTopN={} bgBudget={:.2f} sameGroup={}",
            mixerForegroundTopN,
            mixerBackgroundBudget,
            MixerModeToString(mixerSameGroupMode));
    }

    void HapticsConfig::LoadHidTxConfig(const std::unordered_map<std::string, std::string>& values)
    {
        if (values.count("hid_tx_fg_capacity")) {
            hidTxFgCapacity = std::max<std::uint32_t>(8u, std::stoul(values.at("hid_tx_fg_capacity")));
        }
        if (values.count("hid_tx_bg_capacity")) {
            hidTxBgCapacity = std::max<std::uint32_t>(8u, std::stoul(values.at("hid_tx_bg_capacity")));
        }
        if (values.count("hid_stale_us")) {
            hidStaleUs = std::max<std::uint32_t>(1000u, std::stoul(values.at("hid_stale_us")));
        }
        if (values.count("hid_merge_window_fg_us")) {
            hidMergeWindowFgUs = std::max<std::uint32_t>(0u, std::stoul(values.at("hid_merge_window_fg_us")));
        }
        if (values.count("hid_merge_window_bg_us")) {
            hidMergeWindowBgUs = std::max<std::uint32_t>(0u, std::stoul(values.at("hid_merge_window_bg_us")));
        }
        if (values.count("hid_scheduler_lookahead_us")) {
            hidSchedulerLookaheadUs = std::max<std::uint32_t>(0u, std::stoul(values.at("hid_scheduler_lookahead_us")));
        }
        if (values.count("hid_bg_budget")) {
            hidBgBudget = std::max<std::uint32_t>(1u, std::stoul(values.at("hid_bg_budget")));
        }
        if (values.count("hid_fg_preempt")) {
            hidFgPreempt = ParseBool(values.at("hid_fg_preempt"), hidFgPreempt);
        }
        if (values.count("hid_max_send_per_flush")) {
            hidMaxSendPerFlush = std::clamp<std::uint32_t>(
                std::stoul(values.at("hid_max_send_per_flush")), 1u, 8u);
        }
        if (values.count("hid_min_repeat_interval_us")) {
            hidMinRepeatIntervalUs = std::max<std::uint32_t>(
                1000u, std::stoul(values.at("hid_min_repeat_interval_us")));
        }
        if (values.count("hid_idle_repeat_interval_us")) {
            hidIdleRepeatIntervalUs = std::max<std::uint32_t>(
                2000u, std::stoul(values.at("hid_idle_repeat_interval_us")));
        }
        if (values.count("hid_stop_clears_queue")) {
            hidStopClearsQueue = ParseBool(values.at("hid_stop_clears_queue"), hidStopClearsQueue);
        }
        if (values.count("enable_state_track_scheduler")) {
            enableStateTrackScheduler = ParseBool(
                values.at("enable_state_track_scheduler"), enableStateTrackScheduler);
        }
        if (values.count("enable_state_track_impact_renderer")) {
            enableStateTrackImpactRenderer = ParseBool(
                values.at("enable_state_track_impact_renderer"), enableStateTrackImpactRenderer);
        }
        if (values.count("enable_state_track_swing_renderer")) {
            enableStateTrackSwingRenderer = ParseBool(
                values.at("enable_state_track_swing_renderer"), enableStateTrackSwingRenderer);
        }
        if (values.count("enable_state_track_footstep_renderer")) {
            enableStateTrackFootstepRenderer = ParseBool(
                values.at("enable_state_track_footstep_renderer"), enableStateTrackFootstepRenderer);
        }
        if (values.count("enable_state_track_footstep_token_renderer")) {
            enableStateTrackFootstepTokenRenderer = ParseBool(
                values.at("enable_state_track_footstep_token_renderer"), enableStateTrackFootstepTokenRenderer);
        }
        if (values.count("enable_state_track_footstep_truth_trigger")) {
            enableStateTrackFootstepTruthTrigger = ParseBool(
                values.at("enable_state_track_footstep_truth_trigger"), enableStateTrackFootstepTruthTrigger);
        }
        if (values.count("enable_state_track_footstep_context_gate")) {
            enableStateTrackFootstepContextGate = ParseBool(
                values.at("enable_state_track_footstep_context_gate"), enableStateTrackFootstepContextGate);
        }
        if (values.count("enable_state_track_footstep_motion_gate")) {
            enableStateTrackFootstepMotionGate = ParseBool(
                values.at("enable_state_track_footstep_motion_gate"), enableStateTrackFootstepMotionGate);
        }
        if (values.count("state_track_footstep_recent_move_ms")) {
            stateTrackFootstepRecentMoveMs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_footstep_recent_move_ms")), 0u, 5000u);
        }
        if (values.count("enable_footstep_audio_matcher_shadow")) {
            enableFootstepAudioMatcherShadow = ParseBool(
                values.at("enable_footstep_audio_matcher_shadow"), enableFootstepAudioMatcherShadow);
        }
        if (values.count("footstep_audio_matcher_lookback_us")) {
            footstepAudioMatcherLookbackUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_audio_matcher_lookback_us")), 0u, 120000u);
        }
        if (values.count("footstep_audio_matcher_lookahead_us")) {
            footstepAudioMatcherLookaheadUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_audio_matcher_lookahead_us")), 0u, 120000u);
        }
        if (values.count("footstep_audio_matcher_max_candidates")) {
            footstepAudioMatcherMaxCandidates = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_audio_matcher_max_candidates")), 4u, 512u);
        }
        if (values.count("footstep_audio_matcher_min_score")) {
            footstepAudioMatcherMinScore = std::clamp(
                std::stof(values.at("footstep_audio_matcher_min_score")), 0.0f, 1.0f);
        }
        if (values.count("enable_footstep_truth_bridge_shadow")) {
            enableFootstepTruthBridgeShadow = ParseBool(
                values.at("enable_footstep_truth_bridge_shadow"), enableFootstepTruthBridgeShadow);
        }
        if (values.count("footstep_truth_bridge_lookback_us")) {
            footstepTruthBridgeLookbackUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_truth_bridge_lookback_us")), 1000u, 600000u);
        }
        if (values.count("footstep_truth_bridge_lookahead_us")) {
            footstepTruthBridgeLookaheadUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_truth_bridge_lookahead_us")), 1000u, 800000u);
        }
        if (values.count("footstep_truth_bridge_binding_ttl_ms")) {
            footstepTruthBridgeBindingTtlMs = std::clamp<std::uint32_t>(
                std::stoul(values.at("footstep_truth_bridge_binding_ttl_ms")), 100u, 10000u);
        }
        if (values.count("state_track_lookahead_min_us")) {
            stateTrackLookaheadMinUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_lookahead_min_us")), 800u, 12000u);
        }
        if (values.count("state_track_repeat_keep_max_overdue_us")) {
            stateTrackRepeatKeepMaxOverdueUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_repeat_keep_max_overdue_us")), 0u, 20000u);
        }
        if (values.count("state_track_release_hit_us")) {
            stateTrackReleaseHitUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_release_hit_us")), 12000u, 300000u);
        }
        if (values.count("state_track_release_swing_us")) {
            stateTrackReleaseSwingUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_release_swing_us")), 12000u, 300000u);
        }
        if (values.count("state_track_release_footstep_us")) {
            stateTrackReleaseFootstepUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_release_footstep_us")), 8000u, 300000u);
        }
        if (values.count("state_track_release_utility_us")) {
            stateTrackReleaseUtilityUs = std::clamp<std::uint32_t>(
                std::stoul(values.at("state_track_release_utility_us")), 8000u, 300000u);
        }

        logger::info(
            "[Haptics][Config] HidTx fgCap={} bgCap={} stale={}us mergeFg={}us mergeBg={}us lookahead={}us bgBudget={} fgPreempt={} maxSend={} minRepeat={}us idleRepeat={}us stopClear={} track(en={} impactRender={} swingRender={} footRender={} footToken={} footTruth={} footCtxGate={} footMotionGate={} footRecent={}ms footAudioShadow={} footAudioWin={}/{}us footAudioMaxCand={} footAudioMin={:.2f} footBridge(en={} win={}/{}us ttl={}ms) lookMin={}us keepOverMax={}us rel(hit/swing/foot/util)={}/{}/{}/{})",
            hidTxFgCapacity,
            hidTxBgCapacity,
            hidStaleUs,
            hidMergeWindowFgUs,
            hidMergeWindowBgUs,
            hidSchedulerLookaheadUs,
            hidBgBudget,
            hidFgPreempt,
            hidMaxSendPerFlush,
            hidMinRepeatIntervalUs,
            hidIdleRepeatIntervalUs,
            hidStopClearsQueue,
            enableStateTrackScheduler,
            enableStateTrackImpactRenderer,
            enableStateTrackSwingRenderer,
            enableStateTrackFootstepRenderer,
            enableStateTrackFootstepTokenRenderer,
            enableStateTrackFootstepTruthTrigger,
            enableStateTrackFootstepContextGate,
            enableStateTrackFootstepMotionGate,
            stateTrackFootstepRecentMoveMs,
            enableFootstepAudioMatcherShadow,
            footstepAudioMatcherLookbackUs,
            footstepAudioMatcherLookaheadUs,
            footstepAudioMatcherMaxCandidates,
            footstepAudioMatcherMinScore,
            enableFootstepTruthBridgeShadow,
            footstepTruthBridgeLookbackUs,
            footstepTruthBridgeLookaheadUs,
            footstepTruthBridgeBindingTtlMs,
            stateTrackLookaheadMinUs,
            stateTrackRepeatKeepMaxOverdueUs,
            stateTrackReleaseHitUs,
            stateTrackReleaseSwingUs,
            stateTrackReleaseFootstepUs,
            stateTrackReleaseUtilityUs);
    }

    bool HapticsConfig::IsEventAllowed(EventType type) const
    {
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
            {"shout", EventType::Shout},
            {"ui", EventType::UI},
            {"music", EventType::Music},
            {"ambient", EventType::Ambient}
        };

        auto key = ToLower(str);
        auto it = map.find(key);
        return (it != map.end()) ? it->second : EventType::Unknown;
    }
}
