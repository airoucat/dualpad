#include "pch.h"

#include "input/MenuContextPolicy.h"

#include "input/IniParseHelpers.h"
#include "input/InputContextNames.h"

#include <RE/U/UI.h>

#include <fstream>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        bool HasFlag(std::uint32_t value, RE::UI_MENU_FLAGS flag)
        {
            return (value & static_cast<std::uint32_t>(flag)) != 0;
        }

        bool IsKnownRuntimeInputContext(std::uint32_t rawInputContext)
        {
            using Context = RE::UserEvents::INPUT_CONTEXT_ID;

            switch (static_cast<Context>(rawInputContext)) {
            case Context::kGameplay:
            case Context::kMenuMode:
            case Context::kConsole:
            case Context::kItemMenu:
            case Context::kInventory:
            case Context::kDebugText:
            case Context::kFavorites:
            case Context::kMap:
            case Context::kStats:
            case Context::kCursor:
            case Context::kBook:
            case Context::kDebugOverlay:
            case Context::kJournal:
            case Context::kTFCMode:
            case Context::kMapDebug:
            case Context::kLockpicking:
            case Context::kFavor:
            case Context::kNone:
                return true;
            default:
                return false;
            }
        }

        bool IsStrongTrackRuntimeInputContext(std::uint32_t rawInputContext)
        {
            using Context = RE::UserEvents::INPUT_CONTEXT_ID;

            switch (static_cast<Context>(rawInputContext)) {
            case Context::kConsole:
            case Context::kInventory:
            case Context::kFavorites:
            case Context::kMap:
            case Context::kJournal:
            case Context::kLockpicking:
            case Context::kFavor:
                return true;
            default:
                return false;
            }
        }

        const char* ToString(UnknownMenuPolicy policy)
        {
            switch (policy) {
            case UnknownMenuPolicy::Track:
                return "track";
            case UnknownMenuPolicy::Passthrough:
            default:
                return "passthrough";
            }
        }

        const char* ToString(MenuRuntimeClassification classification)
        {
            switch (classification) {
            case MenuRuntimeClassification::Track:
                return "Track";
            case MenuRuntimeClassification::Passthrough:
                return "Passthrough";
            case MenuRuntimeClassification::Uncertain:
            default:
                return "Uncertain";
            }
        }

        std::string ToString(RE::UserEvents::INPUT_CONTEXT_ID context)
        {
            using Context = RE::UserEvents::INPUT_CONTEXT_ID;

            switch (context) {
            case Context::kGameplay:
                return "kGameplay";
            case Context::kMenuMode:
                return "kMenuMode";
            case Context::kConsole:
                return "kConsole";
            case Context::kItemMenu:
                return "kItemMenu";
            case Context::kInventory:
                return "kInventory";
            case Context::kDebugText:
                return "kDebugText";
            case Context::kFavorites:
                return "kFavorites";
            case Context::kMap:
                return "kMap";
            case Context::kStats:
                return "kStats";
            case Context::kCursor:
                return "kCursor";
            case Context::kBook:
                return "kBook";
            case Context::kDebugOverlay:
                return "kDebugOverlay";
            case Context::kJournal:
                return "kJournal";
            case Context::kTFCMode:
                return "kTFCMode";
            case Context::kMapDebug:
                return "kMapDebug";
            case Context::kLockpicking:
                return "kLockpicking";
            case Context::kFavor:
                return "kFavor";
            case Context::kNone:
                return "kNone";
            default:
                return std::format("unknown({})", static_cast<std::uint32_t>(context));
            }
        }

        std::string DescribeRuntimeInputContext(std::uint32_t rawInputContext)
        {
            return ToString(static_cast<RE::UserEvents::INPUT_CONTEXT_ID>(rawInputContext));
        }

        std::string DescribeMenuFlags(std::uint32_t flags)
        {
            using Flag = RE::UI_MENU_FLAGS;

            struct FlagName
            {
                Flag flag;
                const char* name;
            };

            constexpr FlagName kKnownFlags[] = {
                { Flag::kPausesGame, "PausesGame" },
                { Flag::kAlwaysOpen, "AlwaysOpen" },
                { Flag::kUsesCursor, "UsesCursor" },
                { Flag::kUsesMenuContext, "UsesMenuContext" },
                { Flag::kModal, "Modal" },
                { Flag::kFreezeFrameBackground, "FreezeFrameBackground" },
                { Flag::kOnStack, "OnStack" },
                { Flag::kDisablePauseMenu, "DisablePauseMenu" },
                { Flag::kRequiresUpdate, "RequiresUpdate" },
                { Flag::kTopmostRenderedMenu, "TopmostRenderedMenu" },
                { Flag::kUpdateUsesCursor, "UpdateUsesCursor" },
                { Flag::kAllowSaving, "AllowSaving" },
                { Flag::kRendersOffscreenTargets, "RendersOffscreenTargets" },
                { Flag::kInventoryItemMenu, "InventoryItemMenu" },
                { Flag::kDontHideCursorWhenTopmost, "DontHideCursorWhenTopmost" },
                { Flag::kCustomRendering, "CustomRendering" },
                { Flag::kAssignCursorToRenderer, "AssignCursorToRenderer" },
                { Flag::kApplicationMenu, "ApplicationMenu" },
                { Flag::kHasButtonBar, "HasButtonBar" },
                { Flag::kIsTopButtonBar, "IsTopButtonBar" },
                { Flag::kAdvancesUnderPauseMenu, "AdvancesUnderPauseMenu" },
                { Flag::kRendersUnderPauseMenu, "RendersUnderPauseMenu" },
                { Flag::kUsesBlurredBackground, "UsesBlurredBackground" },
                { Flag::kCompanionAppAllowed, "CompanionAppAllowed" },
                { Flag::kFreezeFramePause, "FreezeFramePause" },
                { Flag::kSkipRenderDuringFreezeFrameScreenshot, "SkipRenderDuringFreezeFrameScreenshot" },
                { Flag::kLargeScaleformRenderCacheMode, "LargeScaleformRenderCacheMode" },
                { Flag::kUsesMovementToDirection, "UsesMovementToDirection" }
            };

            std::string description;
            for (const auto& entry : kKnownFlags) {
                if (!HasFlag(flags, entry.flag)) {
                    continue;
                }

                if (!description.empty()) {
                    description += '|';
                }
                description += entry.name;
            }

            if (description.empty()) {
                description = "None";
            }

            return description;
        }

        std::optional<UnknownMenuPolicy> ParseUnknownMenuPolicyValue(std::string_view value)
        {
            const auto normalized = ini::ToLower(ini::Trim(std::string(value)));
            if (normalized == "passthrough") {
                return UnknownMenuPolicy::Passthrough;
            }
            if (normalized == "track") {
                return UnknownMenuPolicy::Track;
            }
            return std::nullopt;
        }
    }

    MenuContextPolicy& MenuContextPolicy::GetSingleton()
    {
        static MenuContextPolicy instance;
        return instance;
    }

    std::filesystem::path MenuContextPolicy::GetDefaultPath()
    {
        return "Data/SKSE/Plugins/DualPadMenuPolicy.ini";
    }

    bool MenuContextPolicy::Load(const std::filesystem::path& path)
    {
        const auto configPath = path.empty() ? GetDefaultPath() : path;
        MenuContextPolicyConfig parsedConfig;
        MenuContextPolicyConfig effectiveConfig;
        std::vector<std::string> warnings;

        ResetToDefaults();

        logger::info("[DualPad][MenuPolicy] Loading from: {}", configPath.string());

        if (!std::filesystem::exists(configPath)) {
            logger::warn("[DualPad][MenuPolicy] Config file not found, using defaults");
            std::scoped_lock lock(_mutex);
            _configPath = configPath;
            return false;
        }

        std::ifstream stream(configPath);
        if (!stream.is_open()) {
            logger::error("[DualPad][MenuPolicy] Failed to open: {}", configPath.string());
            return false;
        }

        ParseConfig(stream, parsedConfig, &warnings);

        for (const auto& warning : warnings) {
            logger::warn("[DualPad][MenuPolicy] {}", warning);
        }

        {
            std::scoped_lock lock(_mutex);
            _configPath = configPath;
            _config = std::move(parsedConfig);
            effectiveConfig = _config;
        }

        logger::info(
            "[DualPad][MenuPolicy] unknown_menu_policy={} log_probe={} log_decision={} track_rules={} ignore_rules={}",
            ToString(effectiveConfig.unknownMenuPolicy),
            effectiveConfig.logUnknownMenuProbe,
            effectiveConfig.logUnknownMenuDecision,
            effectiveConfig.trackRules.size(),
            effectiveConfig.ignoreRules.size());
        return true;
    }

    bool MenuContextPolicy::Reload()
    {
        std::filesystem::path configPath;

        {
            std::scoped_lock lock(_mutex);
            configPath = _configPath;
        }

        if (configPath.empty()) {
            return Load();
        }

        return Load(configPath);
    }

    std::optional<MenuRuntimeSnapshot> MenuContextPolicy::CaptureRuntimeSnapshot(std::string_view menuName) const
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return std::nullopt;
        }

        auto menu = ui->GetMenu(menuName);
        if (!menu) {
            return std::nullopt;
        }

        return MenuRuntimeSnapshot{
            .inputContextValue = menu->inputContext.underlying(),
            .menuFlagsValue = menu->menuFlags.underlying(),
            .depthPriority = static_cast<std::int32_t>(menu->depthPriority),
            .hasMovie = menu->uiMovie != nullptr,
            .hasDelegate = menu->fxDelegate != nullptr
        };
    }

    MenuTrackingDecision MenuContextPolicy::DecideMenuTracking(
        std::string_view menuName,
        std::optional<MenuRuntimeSnapshot> runtimeSnapshot) const
    {
        MenuContextPolicyConfig config;
        {
            std::scoped_lock lock(_mutex);
            config = _config;
        }

        const auto decision = ResolveMenuTracking(config, menuName, runtimeSnapshot);
        const auto isKnownMenu = KnownMenuNameToContext(menuName).has_value();

        if (!isKnownMenu &&
            config.logUnknownMenuProbe &&
            decision.source != MenuDecisionSource::ExplicitIgnore &&
            decision.source != MenuDecisionSource::ExplicitTrack) {
            LogUnknownMenuProbe(menuName, runtimeSnapshot);
        }

        if (!isKnownMenu && config.logUnknownMenuDecision) {
            LogUnknownMenuDecision(menuName, decision);
        }

        return decision;
    }

    bool MenuContextPolicy::ParseConfig(
        std::istream& stream,
        MenuContextPolicyConfig& outConfig,
        std::vector<std::string>* warnings)
    {
        outConfig = MenuContextPolicyConfig{};

        std::string currentSection;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(stream, line)) {
            ++lineNumber;

            if (lineNumber == 1) {
                ini::StripUtf8Bom(line);
            }

            line = ini::Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                currentSection = ini::Trim(line.substr(1, line.size() - 2));
                continue;
            }

            const auto equalsPos = line.find('=');
            if (equalsPos == std::string::npos) {
                AddWarning(
                    warnings,
                    std::format("Line {}: invalid format (missing '=')", lineNumber));
                continue;
            }

            if (currentSection.empty()) {
                AddWarning(
                    warnings,
                    std::format("Line {}: entry defined before any section", lineNumber));
                continue;
            }

            const auto key = ini::Trim(line.substr(0, equalsPos));
            const auto value = ini::Trim(line.substr(equalsPos + 1));
            if (key.empty()) {
                AddWarning(
                    warnings,
                    std::format("Line {}: empty key", lineNumber));
                continue;
            }

            if (currentSection == "Policy") {
                if (key == "unknown_menu_policy") {
                    if (const auto policy = ParseUnknownMenuPolicyValue(value)) {
                        outConfig.unknownMenuPolicy = *policy;
                    }
                    else {
                        AddWarning(
                            warnings,
                            std::format(
                                "Line {}: invalid unknown_menu_policy='{}'",
                                lineNumber,
                                value));
                    }
                    continue;
                }

                if (key == "log_unknown_menu_probe") {
                    outConfig.logUnknownMenuProbe = ini::ParseBool(value, outConfig.logUnknownMenuProbe);
                    continue;
                }

                if (key == "log_unknown_menu_decision") {
                    outConfig.logUnknownMenuDecision = ini::ParseBool(value, outConfig.logUnknownMenuDecision);
                    continue;
                }

                AddWarning(
                    warnings,
                    std::format("Line {}: unknown policy key '{}'", lineNumber, key));
                continue;
            }

            if (currentSection == "Track") {
                const auto context = ParseInputContextName(value);
                if (!context) {
                    AddWarning(
                        warnings,
                        std::format(
                            "Line {}: invalid tracked context '{}' for menu '{}'",
                            lineNumber,
                            value,
                            key));
                    continue;
                }

                if (!IsMenuOwnedContext(*context)) {
                    AddWarning(
                        warnings,
                        std::format(
                            "Line {}: tracked context '{}' for menu '{}' is not menu-owned",
                            lineNumber,
                            value,
                            key));
                    continue;
                }

                outConfig.ignoreRules.erase(key);
                outConfig.trackRules[key] = *context;
                continue;
            }

            if (currentSection == "Ignore") {
                if (ini::ParseBool(value, false)) {
                    outConfig.trackRules.erase(key);
                    outConfig.ignoreRules.insert(key);
                }
                else {
                    outConfig.ignoreRules.erase(key);
                }
                continue;
            }

            AddWarning(
                warnings,
                std::format("Line {}: unknown section '{}'", lineNumber, currentSection));
        }

        return true;
    }

    MenuTrackingDecision MenuContextPolicy::ResolveMenuTracking(
        const MenuContextPolicyConfig& config,
        std::string_view menuName,
        std::optional<MenuRuntimeSnapshot> runtimeSnapshot)
    {
        const auto key = std::string(menuName);

        if (config.ignoreRules.contains(key)) {
            return MenuTrackingDecision{
                .shouldTrack = false,
                .context = InputContext::Unknown,
                .source = MenuDecisionSource::ExplicitIgnore,
                .classification = MenuRuntimeClassification::Passthrough,
                .usedUnknownPolicy = false
            };
        }

        if (const auto explicitTrack = config.trackRules.find(key);
            explicitTrack != config.trackRules.end()) {
            return MenuTrackingDecision{
                .shouldTrack = true,
                .context = explicitTrack->second,
                .source = MenuDecisionSource::ExplicitTrack,
                .classification = MenuRuntimeClassification::Track,
                .usedUnknownPolicy = false
            };
        }

        if (const auto knownContext = KnownMenuNameToContext(menuName)) {
            return MenuTrackingDecision{
                .shouldTrack = true,
                .context = *knownContext,
                .source = MenuDecisionSource::KnownMenu,
                .classification = MenuRuntimeClassification::Track,
                .usedUnknownPolicy = false
            };
        }

        const auto classification = runtimeSnapshot ?
            ClassifyRuntimeMenu(*runtimeSnapshot) :
            MenuRuntimeClassification::Uncertain;

        if (classification == MenuRuntimeClassification::Track) {
            const auto context = runtimeSnapshot ?
                MapRuntimeInputContext(runtimeSnapshot->inputContextValue).value_or(InputContext::Menu) :
                InputContext::Menu;
            return MenuTrackingDecision{
                .shouldTrack = true,
                .context = context,
                .source = MenuDecisionSource::RuntimeClassification,
                .classification = classification,
                .usedUnknownPolicy = false
            };
        }

        if (classification == MenuRuntimeClassification::Passthrough) {
            return MenuTrackingDecision{
                .shouldTrack = false,
                .context = InputContext::Unknown,
                .source = MenuDecisionSource::RuntimeClassification,
                .classification = classification,
                .usedUnknownPolicy = false
            };
        }

        if (config.unknownMenuPolicy == UnknownMenuPolicy::Track) {
            return MenuTrackingDecision{
                .shouldTrack = true,
                .context = InputContext::Menu,
                .source = MenuDecisionSource::UnknownPolicy,
                .classification = classification,
                .usedUnknownPolicy = true
            };
        }

        return MenuTrackingDecision{
            .shouldTrack = false,
            .context = InputContext::Unknown,
            .source = MenuDecisionSource::UnknownPolicy,
            .classification = classification,
            .usedUnknownPolicy = true
        };
    }

    MenuRuntimeClassification MenuContextPolicy::ClassifyRuntimeMenu(const MenuRuntimeSnapshot& runtimeSnapshot)
    {
        const auto flags = runtimeSnapshot.menuFlagsValue;
        const auto hasTrackFlags = HasFlag(flags, RE::UI_MENU_FLAGS::kPausesGame) ||
            HasFlag(flags, RE::UI_MENU_FLAGS::kUsesMenuContext) ||
            HasFlag(flags, RE::UI_MENU_FLAGS::kModal);

        if (hasTrackFlags || IsStrongTrackRuntimeInputContext(runtimeSnapshot.inputContextValue)) {
            return MenuRuntimeClassification::Track;
        }

        const auto hasPassthroughFlags = HasFlag(flags, RE::UI_MENU_FLAGS::kAlwaysOpen) &&
            HasFlag(flags, RE::UI_MENU_FLAGS::kOnStack) &&
            !HasFlag(flags, RE::UI_MENU_FLAGS::kPausesGame) &&
            !HasFlag(flags, RE::UI_MENU_FLAGS::kUsesMenuContext) &&
            !HasFlag(flags, RE::UI_MENU_FLAGS::kModal);
        const auto isNoneOrUnknownInputContext =
            runtimeSnapshot.inputContextValue == static_cast<std::uint32_t>(RE::UserEvents::INPUT_CONTEXT_ID::kNone) ||
            !IsKnownRuntimeInputContext(runtimeSnapshot.inputContextValue);

        if (hasPassthroughFlags && isNoneOrUnknownInputContext) {
            return MenuRuntimeClassification::Passthrough;
        }

        return MenuRuntimeClassification::Uncertain;
    }

    std::optional<InputContext> MenuContextPolicy::MapRuntimeInputContext(std::uint32_t rawInputContext)
    {
        using Context = RE::UserEvents::INPUT_CONTEXT_ID;

        switch (static_cast<Context>(rawInputContext)) {
        case Context::kConsole:
            return InputContext::Console;
        case Context::kInventory:
            return InputContext::InventoryMenu;
        case Context::kFavorites:
            return InputContext::FavoritesMenu;
        case Context::kMap:
            return InputContext::MapMenu;
        case Context::kJournal:
            return InputContext::JournalMenu;
        case Context::kLockpicking:
            return InputContext::Lockpicking;
        case Context::kFavor:
            return InputContext::Favor;
        default:
            return std::nullopt;
        }
    }

    std::optional<InputContext> MenuContextPolicy::KnownMenuNameToContext(std::string_view menuName)
    {
        if (menuName == RE::InventoryMenu::MENU_NAME) return InputContext::InventoryMenu;
        if (menuName == RE::MagicMenu::MENU_NAME) return InputContext::MagicMenu;
        if (menuName == RE::MapMenu::MENU_NAME) return InputContext::MapMenu;
        if (menuName == RE::JournalMenu::MENU_NAME) return InputContext::JournalMenu;

        if (menuName == "DialogueMenu" || menuName == "Dialogue Menu") return InputContext::DialogueMenu;
        if (menuName == "FavoritesMenu" || menuName == "Favorites Menu") return InputContext::FavoritesMenu;
        if (menuName == "TweenMenu" || menuName == "Tween Menu") return InputContext::TweenMenu;
        if (menuName == "ContainerMenu" || menuName == "Container Menu") return InputContext::ContainerMenu;
        if (menuName == "BarterMenu" || menuName == "Barter Menu") return InputContext::BarterMenu;
        if (menuName == "Training Menu") return InputContext::TrainingMenu;
        if (menuName == "LevelUp Menu") return InputContext::LevelUpMenu;
        if (menuName == "RaceSex Menu") return InputContext::RaceSexMenu;
        if (menuName == "StatsMenu" || menuName == "Stats Menu") return InputContext::StatsMenu;
        if (menuName == "SkillMenu" || menuName == "Skill Menu") return InputContext::SkillMenu;
        if (menuName == "Book Menu" || menuName == "BookMenu") return InputContext::BookMenu;
        if (menuName == "MessageBoxMenu" || menuName == "MessageBox Menu") return InputContext::MessageBoxMenu;
        if (menuName == "QuantityMenu" || menuName == "Quantity Menu") return InputContext::QuantityMenu;
        if (menuName == "GiftMenu" || menuName == "Gift Menu") return InputContext::GiftMenu;
        if (menuName == "Creations Menu" ||
            menuName == "Creation Club Menu" ||
            menuName == "Mod Manager Menu") {
            return InputContext::CreationsMenu;
        }

        if (menuName == "Console" || menuName == "Console Native UI Menu") return InputContext::Console;
        if (menuName == "Lockpicking Menu" || menuName == "LockpickingMenu") return InputContext::Lockpicking;
        if (menuName == "Loading Menu" ||
            menuName == "Main Menu" ||
            menuName == "Credits Menu" ||
            menuName == "Crafting Menu" ||
            menuName == "TitleSequence Menu" ||
            menuName == "Sleep/Wait Menu" ||
            menuName == "Kinect Menu" ||
            menuName == "SafeZoneMenu" ||
            menuName == "StreamingInstallMenu") {
            return InputContext::Menu;
        }

        return std::nullopt;
    }

    void MenuContextPolicy::ResetToDefaults()
    {
        std::scoped_lock lock(_mutex);
        _config = MenuContextPolicyConfig{};
    }

    void MenuContextPolicy::AddWarning(std::vector<std::string>* warnings, std::string message)
    {
        if (warnings) {
            warnings->push_back(std::move(message));
        }
    }

    void MenuContextPolicy::LogUnknownMenuProbe(
        std::string_view menuName,
        const std::optional<MenuRuntimeSnapshot>& runtimeSnapshot)
    {
        if (!runtimeSnapshot) {
            logger::warn(
                "[DualPad][MenuPolicy] Unknown menu runtime probe '{}' skipped: menu unavailable",
                menuName);
            return;
        }

        logger::warn(
            "[DualPad][MenuPolicy] Unknown menu runtime probe '{}' inputContext={}({}) menuFlags=0x{:08X} flags=[{}] depth={} movie={} delegate={}",
            menuName,
            runtimeSnapshot->inputContextValue,
            DescribeRuntimeInputContext(runtimeSnapshot->inputContextValue),
            runtimeSnapshot->menuFlagsValue,
            DescribeMenuFlags(runtimeSnapshot->menuFlagsValue),
            runtimeSnapshot->depthPriority,
            runtimeSnapshot->hasMovie,
            runtimeSnapshot->hasDelegate);
    }

    void MenuContextPolicy::LogUnknownMenuDecision(
        std::string_view menuName,
        const MenuTrackingDecision& decision)
    {
        switch (decision.source) {
        case MenuDecisionSource::ExplicitTrack:
            logger::info(
                "[DualPad][MenuPolicy] Unknown menu '{}' explicitly tracked as {}",
                menuName,
                ToString(decision.context));
            return;
        case MenuDecisionSource::ExplicitIgnore:
            logger::info(
                "[DualPad][MenuPolicy] Unknown menu '{}' explicitly ignored",
                menuName);
            return;
        case MenuDecisionSource::RuntimeClassification:
            if (decision.classification == MenuRuntimeClassification::Track) {
                logger::info(
                    "[DualPad][MenuPolicy] Unknown menu '{}' classified as Track -> {}",
                    menuName,
                    ToString(decision.context));
                return;
            }

            if (decision.classification == MenuRuntimeClassification::Passthrough) {
                logger::info(
                    "[DualPad][MenuPolicy] Unknown menu '{}' classified as Passthrough",
                    menuName);
                return;
            }
            break;
        case MenuDecisionSource::UnknownPolicy:
            logger::info(
                "[DualPad][MenuPolicy] Unknown menu '{}' classified as {} -> policy {}",
                menuName,
                ToString(decision.classification),
                decision.shouldTrack ? "track" : "passthrough");
            return;
        case MenuDecisionSource::KnownMenu:
        default:
            return;
        }
    }
}
