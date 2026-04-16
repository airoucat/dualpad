#pragma once

#include "input/InputContext.h"

#include <cstdint>
#include <filesystem>
#include <istream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dualpad::input
{
    enum class UnknownMenuPolicy
    {
        Passthrough = 0,
        Track
    };

    enum class MenuRuntimeClassification
    {
        Track = 0,
        Passthrough,
        Uncertain
    };

    enum class MenuDecisionSource
    {
        ExplicitTrack = 0,
        ExplicitIgnore,
        KnownMenu,
        RuntimeClassification,
        UnknownPolicy
    };

    // Runtime facts sampled from RE::IMenu when a menu name is not in our built-in map.
    struct MenuRuntimeSnapshot
    {
        std::uint32_t inputContextValue{ 0 };
        std::uint32_t menuFlagsValue{ 0 };
        std::int32_t depthPriority{ 0 };
        bool hasMovie{ false };
        bool hasDelegate{ false };
    };

    struct MenuTrackingDecision
    {
        bool shouldTrack{ false };
        InputContext context{ InputContext::Unknown };
        MenuDecisionSource source{ MenuDecisionSource::UnknownPolicy };
        MenuRuntimeClassification classification{ MenuRuntimeClassification::Uncertain };
        bool usedUnknownPolicy{ false };
    };

    struct MenuContextPolicyConfig
    {
        UnknownMenuPolicy unknownMenuPolicy{ UnknownMenuPolicy::Passthrough };
        bool logUnknownMenuProbe{ true };
        bool logUnknownMenuDecision{ true };
        std::unordered_map<std::string, InputContext> trackRules;
        std::unordered_set<std::string> ignoreRules;
    };

    // Resolves whether a menu should claim a logical InputContext via config, known names,
    // and runtime heuristics.
    class MenuContextPolicy
    {
    public:
        static MenuContextPolicy& GetSingleton();

        bool Load(const std::filesystem::path& path = {});
        bool Reload();
        static std::filesystem::path GetDefaultPath();

        std::optional<MenuRuntimeSnapshot> CaptureRuntimeSnapshot(std::string_view menuName) const;
        MenuTrackingDecision DecideMenuTracking(
            std::string_view menuName,
            std::optional<MenuRuntimeSnapshot> runtimeSnapshot = std::nullopt) const;

        static bool ParseConfig(
            std::istream& stream,
            MenuContextPolicyConfig& outConfig,
            std::vector<std::string>* warnings = nullptr);

        static MenuTrackingDecision ResolveMenuTracking(
            const MenuContextPolicyConfig& config,
            std::string_view menuName,
            std::optional<MenuRuntimeSnapshot> runtimeSnapshot = std::nullopt);

        static MenuRuntimeClassification ClassifyRuntimeMenu(const MenuRuntimeSnapshot& runtimeSnapshot);
        static std::optional<InputContext> MapRuntimeInputContext(std::uint32_t rawInputContext);
        static std::optional<InputContext> KnownMenuNameToContext(std::string_view menuName);

    private:
        MenuContextPolicy() = default;

        void ResetToDefaults();
        static void AddWarning(std::vector<std::string>* warnings, std::string message);
        static void LogUnknownMenuProbe(
            std::string_view menuName,
            const std::optional<MenuRuntimeSnapshot>& runtimeSnapshot);
        static void LogUnknownMenuDecision(
            std::string_view menuName,
            const MenuTrackingDecision& decision);

        std::filesystem::path _configPath;
        MenuContextPolicyConfig _config{};
        mutable std::mutex _mutex;
    };
}
