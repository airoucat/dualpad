#include "pch.h"

#include "input/MenuContextPolicy.h"

#include <sstream>
#include <stdexcept>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    void TestParseConfig()
    {
        std::istringstream input(R"ini(
[Policy]
unknown_menu_policy = track
log_unknown_menu_probe = false
log_unknown_menu_decision = true

[Track]
BTPS Menu = Menu
BestiaryWidget = JournalMenu
Gameplay = Gameplay
BrokenMenu = NotAContext

[Ignore]
HUD Menu = true
Cursor Menu = false
)ini");

        dualpad::input::MenuContextPolicyConfig config;
        std::vector<std::string> warnings;
        const auto parsed = dualpad::input::MenuContextPolicy::ParseConfig(input, config, &warnings);

        Require(parsed, "config parse should succeed");
        Require(config.unknownMenuPolicy == dualpad::input::UnknownMenuPolicy::Track, "unknown policy should parse");
        Require(!config.logUnknownMenuProbe, "probe logging should parse");
        Require(config.logUnknownMenuDecision, "decision logging should parse");
        Require(config.trackRules.at("BTPS Menu") == dualpad::input::InputContext::Menu, "track rule should parse");
        Require(
            config.trackRules.at("BestiaryWidget") == dualpad::input::InputContext::JournalMenu,
            "journal track rule should parse");
        Require(config.ignoreRules.contains("HUD Menu"), "ignore rule should parse");
        Require(!config.ignoreRules.contains("Cursor Menu"), "false ignore rule should be removed");
        Require(!config.trackRules.contains("Gameplay"), "gameplay context should be rejected");
        Require(warnings.size() == 2, "invalid track entries should emit warnings");
    }

    void TestRuntimeClassification()
    {
        using MenuRuntimeClassification = dualpad::input::MenuRuntimeClassification;
        using MenuRuntimeSnapshot = dualpad::input::MenuRuntimeSnapshot;

        const auto trackByFlags = dualpad::input::MenuContextPolicy::ClassifyRuntimeMenu(MenuRuntimeSnapshot{
            .inputContextValue = static_cast<std::uint32_t>(RE::UserEvents::INPUT_CONTEXT_ID::kNone),
            .menuFlagsValue = static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kPausesGame),
            .depthPriority = 0,
            .hasMovie = true,
            .hasDelegate = true
        });
        Require(trackByFlags == MenuRuntimeClassification::Track, "pause flag should classify as track");

        const auto trackByInputContext = dualpad::input::MenuContextPolicy::ClassifyRuntimeMenu(MenuRuntimeSnapshot{
            .inputContextValue = static_cast<std::uint32_t>(RE::UserEvents::INPUT_CONTEXT_ID::kFavorites),
            .menuFlagsValue = static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kAlwaysOpen) |
                static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kOnStack),
            .depthPriority = 0,
            .hasMovie = true,
            .hasDelegate = true
        });
        Require(trackByInputContext == MenuRuntimeClassification::Track, "favorites context should classify as track");

        const auto passthroughOverlay = dualpad::input::MenuContextPolicy::ClassifyRuntimeMenu(MenuRuntimeSnapshot{
            .inputContextValue = 18,
            .menuFlagsValue = static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kAlwaysOpen) |
                static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kOnStack) |
                static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kRequiresUpdate) |
                static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kAllowSaving),
            .depthPriority = 0,
            .hasMovie = true,
            .hasDelegate = true
        });
        Require(
            passthroughOverlay == MenuRuntimeClassification::Passthrough,
            "overlay-style flags with unknown context should classify as passthrough");

        const auto uncertain = dualpad::input::MenuContextPolicy::ClassifyRuntimeMenu(MenuRuntimeSnapshot{
            .inputContextValue = static_cast<std::uint32_t>(RE::UserEvents::INPUT_CONTEXT_ID::kCursor),
            .menuFlagsValue = static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kAlwaysOpen) |
                static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kOnStack),
            .depthPriority = 0,
            .hasMovie = true,
            .hasDelegate = true
        });
        Require(uncertain == MenuRuntimeClassification::Uncertain, "cursor overlays should remain uncertain");
    }

    void TestDecisionOrder()
    {
        using dualpad::input::InputContext;
        using dualpad::input::MenuContextPolicy;
        using dualpad::input::MenuContextPolicyConfig;
        using dualpad::input::MenuDecisionSource;
        using dualpad::input::MenuRuntimeSnapshot;
        using dualpad::input::UnknownMenuPolicy;

        MenuContextPolicyConfig config;
        config.unknownMenuPolicy = UnknownMenuPolicy::Passthrough;
        config.ignoreRules.insert("HUD Menu");
        config.trackRules.emplace("BTPS Menu", InputContext::Menu);

        const auto explicitIgnore = MenuContextPolicy::ResolveMenuTracking(config, "HUD Menu");
        Require(!explicitIgnore.shouldTrack, "explicit ignore should win");
        Require(explicitIgnore.source == MenuDecisionSource::ExplicitIgnore, "ignore source should be explicit");

        const auto explicitTrack = MenuContextPolicy::ResolveMenuTracking(config, "BTPS Menu");
        Require(explicitTrack.shouldTrack, "explicit track should win");
        Require(explicitTrack.context == InputContext::Menu, "explicit track should target menu");
        Require(explicitTrack.source == MenuDecisionSource::ExplicitTrack, "track source should be explicit");

        const auto knownMenu = MenuContextPolicy::ResolveMenuTracking(config, "FavoritesMenu");
        Require(knownMenu.shouldTrack, "known menu should track");
        Require(knownMenu.context == InputContext::FavoritesMenu, "known menu should keep mapped context");
        Require(knownMenu.source == MenuDecisionSource::KnownMenu, "known menu source should be known");

        const auto runtimeTrack = MenuContextPolicy::ResolveMenuTracking(
            config,
            "ThirdPartyFavoritesMenu",
            MenuRuntimeSnapshot{
                .inputContextValue = static_cast<std::uint32_t>(RE::UserEvents::INPUT_CONTEXT_ID::kFavorites),
                .menuFlagsValue = 0,
                .depthPriority = 0,
                .hasMovie = true,
                .hasDelegate = true
            });
        Require(runtimeTrack.shouldTrack, "runtime strong signal should track");
        Require(runtimeTrack.context == InputContext::FavoritesMenu, "runtime track should use mapped menu context");
        Require(runtimeTrack.source == MenuDecisionSource::RuntimeClassification, "runtime source should classify");

        const auto passthroughFallback = MenuContextPolicy::ResolveMenuTracking(config, "UnknownWidget");
        Require(!passthroughFallback.shouldTrack, "passthrough policy should not track unknown menu");
        Require(
            passthroughFallback.source == MenuDecisionSource::UnknownPolicy,
            "unknown fallback should use policy source");

        config.unknownMenuPolicy = UnknownMenuPolicy::Track;
        const auto trackFallback = MenuContextPolicy::ResolveMenuTracking(config, "UnknownWidget");
        Require(trackFallback.shouldTrack, "track policy should track unknown menu");
        Require(trackFallback.context == InputContext::Menu, "track policy should use generic menu");
    }
}

int main()
{
    TestParseConfig();
    TestRuntimeClassification();
    TestDecisionOrder();
    return 0;
}
