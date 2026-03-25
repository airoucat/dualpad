#include "pch.h"
#include "input/BindingConfig.h"
#include "input/BindingManager.h"
#include "input/IniParseHelpers.h"
#include "input/InputContext.h"
#include "input/Trigger.h"
#include "input/mapping/PadEvent.h"
#include "input/mapping/TouchpadMapper.h"

#include <SKSE/SKSE.h>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        InputContext StringToContext(std::string_view str)
        {
            if (str == "Gameplay") return InputContext::Gameplay;
            if (str == "Menu") return InputContext::Menu;
            if (str == "InventoryMenu") return InputContext::InventoryMenu;
            if (str == "MagicMenu") return InputContext::MagicMenu;
            if (str == "MapMenu") return InputContext::MapMenu;
            if (str == "Map Menu") return InputContext::MapMenu;
            if (str == "JournalMenu") return InputContext::JournalMenu;
            if (str == "Journal Menu") return InputContext::JournalMenu;
            if (str == "DialogueMenu") return InputContext::DialogueMenu;
            if (str == "Dialogue Menu") return InputContext::DialogueMenu;
            if (str == "FavoritesMenu") return InputContext::FavoritesMenu;
            if (str == "Favorites Menu") return InputContext::FavoritesMenu;
            if (str == "TweenMenu") return InputContext::TweenMenu;
            if (str == "Tween Menu") return InputContext::TweenMenu;
            if (str == "ContainerMenu") return InputContext::ContainerMenu;
            if (str == "Container Menu") return InputContext::ContainerMenu;
            if (str == "BarterMenu") return InputContext::BarterMenu;
            if (str == "Barter Menu") return InputContext::BarterMenu;
            if (str == "TrainingMenu" || str == "Training Menu") return InputContext::TrainingMenu;
            if (str == "LevelUpMenu" || str == "LevelUp Menu") return InputContext::LevelUpMenu;
            if (str == "RaceSexMenu" || str == "RaceSex Menu") return InputContext::RaceSexMenu;
            if (str == "StatsMenu" || str == "Stats Menu") return InputContext::StatsMenu;
            // Project-reserved alias for modded UI. Vanilla SE 1.5.97 skill/perk
            // flow normally stays inside StatsMenu.
            if (str == "SkillMenu" || str == "Skill Menu") return InputContext::SkillMenu;
            if (str == "BookMenu" || str == "Book Menu") return InputContext::BookMenu;
            if (str == "MessageBoxMenu" || str == "MessageBox Menu") return InputContext::MessageBoxMenu;
            if (str == "QuantityMenu" || str == "Quantity Menu") return InputContext::QuantityMenu;
            if (str == "GiftMenu" || str == "Gift Menu") return InputContext::GiftMenu;
            if (str == "Lockpicking") return InputContext::Lockpicking;
            if (str == "LockpickingMenu" || str == "Lockpicking Menu") return InputContext::Lockpicking;
            if (str == "Combat") return InputContext::Combat;
            if (str == "Sneaking") return InputContext::Sneaking;
            if (str == "Riding") return InputContext::Riding;
            if (str == "Werewolf") return InputContext::Werewolf;
            if (str == "VampireLord") return InputContext::VampireLord;
            if (str == "Console") return InputContext::Console;
            if (str == "Console Native UI Menu") return InputContext::Console;
            if (str == "Book") return InputContext::Book;
            if (str == "CreationsMenu" || str == "Creations Menu") return InputContext::CreationsMenu;
            if (str == "CreationClubMenu" || str == "Creation Club Menu") return InputContext::CreationsMenu;
            if (str == "Mod Manager Menu") return InputContext::CreationsMenu;
            if (str == "ItemMenu" || str == "Item Menu") return InputContext::ItemMenu;
            if (str == "DebugText" || str == "Debug Text Menu") return InputContext::DebugText;
            if (str == "MapMenuContext") return InputContext::MapMenuContext;
            if (str == "Stats") return InputContext::Stats;
            if (str == "Cursor" || str == "Cursor Menu" || str == "CursorMenu") return InputContext::Cursor;
            if (str == "DebugOverlay") return InputContext::DebugOverlay;
            if (str == "TFCMode") return InputContext::TFCMode;
            if (str == "DebugMapMenu") return InputContext::DebugMapMenu;
            if (str == "Favor") return InputContext::Favor;
            if (str == "Death") return InputContext::Death;
            if (str == "Bleedout") return InputContext::Bleedout;
            if (str == "Ragdoll") return InputContext::Ragdoll;
            if (str == "KillMove") return InputContext::KillMove;

            return InputContext::Unknown;
        }

        std::uint32_t GestureNameToCode(std::string_view name)
        {
            using namespace mapping_codes;
            if (name == "TpLeftPress") return kTpLeftPress;
            if (name == "TpMidPress" || name == "TpCenterPress") return kTpMidPress;
            if (name == "TpRightPress") return kTpRightPress;
            if (name == "TpSwipeUp") return kTpSwipeUp;
            if (name == "TpSwipeDown") return kTpSwipeDown;
            if (name == "TpSwipeLeft") return kTpSwipeLeft;
            if (name == "TpSwipeRight") return kTpSwipeRight;
            if (name == "TpEdgeTopPress") return kTpEdgeTopPress;
            if (name == "TpEdgeBottomPress") return kTpEdgeBottomPress;
            if (name == "TpEdgeLeftPress") return kTpEdgeLeftPress;
            if (name == "TpEdgeRightPress") return kTpEdgeRightPress;
            if (name == "TpWholePress") return kTpWholePress;
            return 0;
        }

        std::uint32_t ButtonNameToCode(std::string_view name)
        {
            if (name == "Square") return 0x00000001;
            if (name == "Cross") return 0x00000002;
            if (name == "Circle") return 0x00000004;
            if (name == "Triangle") return 0x00000008;
            if (name == "L1") return 0x00000010;
            if (name == "R1") return 0x00000020;
            if (name == "L2Button") return 0x00000040;
            if (name == "R2Button") return 0x00000080;
            if (name == "Create") return 0x00000100;
            if (name == "Options") return 0x00000200;
            if (name == "L3") return 0x00000400;
            if (name == "R3") return 0x00000800;
            if (name == "PS") return 0x00001000;
            if (name == "Mute" || name == "Mic") return 0x00002000;
            if (name == "TouchpadClick") return 0x00004000;
            if (name == "DpadUp") return 0x00010000;
            if (name == "DpadDown") return 0x00020000;
            if (name == "DpadLeft") return 0x00040000;
            if (name == "DpadRight") return 0x00080000;
            if (name == "FnLeft") return 0x00100000;
            if (name == "FnRight") return 0x00200000;
            if (name == "BackLeft") return 0x00400000;
            if (name == "BackRight") return 0x00800000;
            return 0;
        }

        bool IsFaceButtonCode(std::uint32_t code)
        {
            switch (code) {
            case 0x00000001:
            case 0x00000002:
            case 0x00000004:
            case 0x00000008:
                return true;
            default:
                return false;
            }
        }

        bool IsFnButtonCode(std::uint32_t code)
        {
            return code == 0x00100000 || code == 0x00200000;
        }

        bool ContainsFnWithFace(const std::vector<std::uint32_t>& buttons)
        {
            bool hasFace = false;
            bool hasFn = false;
            for (const auto code : buttons) {
                hasFace = hasFace || IsFaceButtonCode(code);
                if (IsFnButtonCode(code)) {
                    hasFn = true;
                }
            }

            return hasFace && hasFn;
        }

        bool ParseButtonList(std::string_view chord, std::vector<std::uint32_t>& outCodes)
        {
            outCodes.clear();
            std::size_t tokenStart = 0;

            while (tokenStart <= chord.size()) {
                const auto plusPos = chord.find('+', tokenStart);
                const auto tokenView = plusPos == std::string_view::npos ?
                    chord.substr(tokenStart) :
                    chord.substr(tokenStart, plusPos - tokenStart);

                const auto token = ini::Trim(std::string(tokenView));
                if (token.empty()) {
                    return false;
                }

                const auto code = ButtonNameToCode(token);
                if (code == 0) {
                    return false;
                }

                if (std::find(outCodes.begin(), outCodes.end(), code) != outCodes.end()) {
                    return false;
                }

                outCodes.push_back(code);

                if (plusPos == std::string_view::npos) {
                    break;
                }

                tokenStart = plusPos + 1;
            }

            if (outCodes.empty()) {
                return false;
            }

            if (ContainsFnWithFace(outCodes)) {
                logger::warn("[DualPad][Config] Rejecting forbidden FN+Face chord");
                return false;
            }

            return true;
        }

        bool ParseButtonChord(std::string_view chord, Trigger& outTrigger)
        {
            std::vector<std::uint32_t> parsedCodes;
            if (!ParseButtonList(chord, parsedCodes)) {
                return false;
            }

            outTrigger.code = parsedCodes.back();
            outTrigger.modifiers.assign(parsedCodes.begin(), parsedCodes.end() - 1);
            (std::sort)(outTrigger.modifiers.begin(), outTrigger.modifiers.end());
            return true;
        }

        bool ParseUnorderedCombo(std::string_view chord, Trigger& outTrigger)
        {
            std::vector<std::uint32_t> parsedCodes;
            if (!ParseButtonList(chord, parsedCodes)) {
                return false;
            }

            if (parsedCodes.size() != 2) {
                logger::warn(
                    "[DualPad][Config] Combo:* requires exactly two digital buttons ({})",
                    chord);
                return false;
            }

            (std::sort)(parsedCodes.begin(), parsedCodes.end());
            outTrigger.code = parsedCodes[1];
            outTrigger.modifiers = { parsedCodes[0] };
            return true;
        }

        bool ParseSingleButton(std::string_view buttonName, Trigger& outTrigger)
        {
            const auto token = ini::Trim(std::string(buttonName));
            if (token.empty()) {
                return false;
            }

            if (token.find('+') != std::string::npos) {
                logger::warn(
                    "[DualPad][Config] Button:* no longer accepts modifier chords ({}). "
                    "Use Layer:* for strict ordered modifier actions.",
                    token);
                return false;
            }

            const auto code = ButtonNameToCode(token);
            if (code == 0) {
                return false;
            }

            outTrigger.code = code;
            outTrigger.modifiers.clear();
            return true;
        }

        std::uint32_t AxisNameToCode(std::string_view name)
        {
            if (name == "LeftStickX") return static_cast<std::uint32_t>(PadAxisId::LeftStickX);
            if (name == "LeftStickY") return static_cast<std::uint32_t>(PadAxisId::LeftStickY);
            if (name == "RightStickX") return static_cast<std::uint32_t>(PadAxisId::RightStickX);
            if (name == "RightStickY") return static_cast<std::uint32_t>(PadAxisId::RightStickY);
            if (name == "LeftTrigger") return static_cast<std::uint32_t>(PadAxisId::LeftTrigger);
            if (name == "RightTrigger") return static_cast<std::uint32_t>(PadAxisId::RightTrigger);
            return 0;
        }

        bool ParseFloat(std::string_view text, float& outValue)
        {
            std::string buffer(text);
            char* end = nullptr;
            const auto value = std::strtof(buffer.c_str(), &end);
            if (end == buffer.c_str() || end == nullptr || *end != '\0') {
                return false;
            }

            outValue = value;
            return true;
        }

        std::optional<InputContext> ResolveContextName(std::string_view str)
        {
            const auto context = StringToContext(str);
            if (context == InputContext::Unknown) {
                return std::nullopt;
            }

            return context;
        }

    }

    BindingConfig& BindingConfig::GetSingleton()
    {
        static BindingConfig instance;
        return instance;
    }

    std::filesystem::path BindingConfig::GetDefaultPath()
    {
        return "Data/SKSE/Plugins/DualPadBindings.ini";
    }

    TouchpadConfig BindingConfig::GetTouchpadConfig() const
    {
        return _touchpadConfig;
    }

    bool BindingConfig::Load(const std::filesystem::path& path)
    {
        _configPath = path.empty() ? GetDefaultPath() : path;

        logger::info("[DualPad][Config] Loading bindings from: {}", _configPath.string());
        BindingManager::GetSingleton().ClearBindings();
        _touchpadConfig = {};

        if (!std::filesystem::exists(_configPath)) {
            logger::warn("[DualPad][Config] Config file not found, using defaults");
            BindingManager::GetSingleton().InitDefaultBindings();
            BindingManager::GetSingleton().ApplyStandardFallbackBindings();
            return false;
        }

        return ParseIniFile(_configPath);
    }

    bool BindingConfig::Reload()
    {
        if (_configPath.empty()) {
            return Load();
        }
        BindingManager::GetSingleton().ClearBindings();
        _touchpadConfig = {};
        return ParseIniFile(_configPath);
    }

    // Accepts Gesture:Name, Button:Name, Layer:A+B, Combo:A+B, Hold:Name, Tap:Name, or Axis:Name.
    bool BindingConfig::ParseTrigger(std::string_view triggerStr, Trigger& outTrigger)
    {

        auto colonPos = triggerStr.find(':');
        if (colonPos == std::string_view::npos) {
            return false;
        }

        auto typeStr = triggerStr.substr(0, colonPos);
        auto codeStr = triggerStr.substr(colonPos + 1);

        if (typeStr == "Gesture") {
            outTrigger.type = TriggerType::Gesture;
            outTrigger.code = GestureNameToCode(codeStr);
            return outTrigger.code != 0;
        }

        const auto parseChordLikeTrigger = [&](TriggerType type) {
            outTrigger.type = type;
            outTrigger.modifiers.clear();
            return ParseButtonChord(codeStr, outTrigger);
            };

        if (typeStr == "Button") {
            outTrigger.type = TriggerType::Button;
            return ParseSingleButton(codeStr, outTrigger);
        }

        if (typeStr == "Layer") {
            return parseChordLikeTrigger(TriggerType::Layer);
        }

        if (typeStr == "Combo") {
            outTrigger.type = TriggerType::Combo;
            outTrigger.modifiers.clear();
            return ParseUnorderedCombo(codeStr, outTrigger);
        }

        if (typeStr == "Hold") {
            return parseChordLikeTrigger(TriggerType::Hold);
        }

        if (typeStr == "Tap") {
            return parseChordLikeTrigger(TriggerType::Tap);
        }

        if (typeStr == "Axis") {
            outTrigger.type = TriggerType::Axis;
            outTrigger.modifiers.clear();
            outTrigger.code = AxisNameToCode(codeStr);
            return outTrigger.code != 0;
        }

        return false;
    }

    bool BindingConfig::ParseTouchpadSetting(std::string_view key, std::string_view value)
    {
        if (key == "Mode") {
            if (value == "LeftCenterRight" || value == "LCR") {
                _touchpadConfig.mode = TouchpadMode::LeftCenterRight;
                return true;
            }
            if (value == "Edge") {
                _touchpadConfig.mode = TouchpadMode::Edge;
                return true;
            }
            if (value == "Whole") {
                _touchpadConfig.mode = TouchpadMode::Whole;
                return true;
            }
            if (value == "Disabled") {
                _touchpadConfig.mode = TouchpadMode::Disabled;
                return true;
            }

            return false;
        }

        if (key == "EdgeThreshold") {
            return ParseFloat(value, _touchpadConfig.edgeThreshold);
        }

        if (key == "LeftRightBoundary") {
            return ParseFloat(value, _touchpadConfig.leftRightBoundary);
        }

        if (key == "SlideThreshold") {
            return ParseFloat(value, _touchpadConfig.slideThreshold);
        }

        return false;
    }

    bool BindingConfig::ParseBinding(std::string_view contextStr, std::string_view key, std::string_view value)
    {
        auto context = StringToContext(contextStr);
        if (context == InputContext::Unknown) {
            logger::warn("[DualPad][Config] Unknown context: {}", contextStr);
            return false;
        }

        Trigger trigger;
        if (!ParseTrigger(key, trigger)) {
            logger::warn("[DualPad][Config] Invalid trigger: {}", key);
            return false;
        }

        Binding binding;
        binding.trigger = trigger;
        binding.actionId = std::string(value);
        binding.context = context;

        BindingManager::GetSingleton().AddBinding(binding);

        logger::trace("[DualPad][Config] Bound {} -> {} in {}",
            key, value, contextStr);

        return true;
    }

    bool BindingConfig::ParseIniFile(const std::filesystem::path& path)
    {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            logger::error("[DualPad][Config] Failed to open: {}", path.string());
            return false;
        }

        std::string currentSection;
        std::string line;
        std::size_t lineNo = 0;
        std::size_t bindingCount = 0;
        std::unordered_map<InputContext, InputContext> inheritMap;

        // The parser is intentionally simple because the file format is small and user-editable.
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
                currentSection = ini::Trim(line.substr(1, line.size() - 2));
                logger::info("[DualPad][Config] Parsing section: [{}]", currentSection);
                continue;
            }

            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                logger::warn("[DualPad][Config] Line {}: Invalid format (no '=')", lineNo);
                continue;
            }

            auto key = ini::Trim(line.substr(0, eqPos));
            auto value = ini::Trim(line.substr(eqPos + 1));

            if (key.empty() || value.empty()) {
                continue;
            }

            if (currentSection.empty()) {
                logger::warn("[DualPad][Config] Line {}: No section defined", lineNo);
                continue;
            }

            if (currentSection == "Touchpad") {
                if (!ParseTouchpadSetting(key, value)) {
                    logger::warn("[DualPad][Config] Invalid touchpad setting: {}={}", key, value);
                }
                continue;
            }

            if (key == "Inherit") {
                const auto child = ResolveContextName(currentSection);
                const auto parent = ResolveContextName(value);
                if (!child || !parent) {
                    logger::warn("[DualPad][Config] Invalid inherit rule: [{}] Inherit={}", currentSection, value);
                    continue;
                }

                inheritMap[*child] = *parent;
                logger::info("[DualPad][Config] Context {} inherits from {}", currentSection, value);
                continue;
            }

            if (ParseBinding(currentSection, key, value)) {
                ++bindingCount;
            }
        }

        std::unordered_map<InputContext, std::uint8_t> visitState;
        std::function<void(InputContext)> applyInheritance = [&](InputContext child) {
            const auto state = visitState[child];
            if (state == 2) {
                return;
            }
            if (state == 1) {
                logger::warn("[DualPad][Config] Inheritance cycle detected at context {}", ToString(child));
                return;
            }

            visitState[child] = 1;

            const auto inheritIt = inheritMap.find(child);
            if (inheritIt == inheritMap.end()) {
                visitState[child] = 2;
                return;
            }

            applyInheritance(inheritIt->second);
            BindingManager::GetSingleton().MergeBindings(inheritIt->second, child, false);
            visitState[child] = 2;
        };

        for (const auto& [child, parent] : inheritMap) {
            (void)parent;
            applyInheritance(child);
        }

        const auto fallbackCount = BindingManager::GetSingleton().ApplyStandardFallbackBindings();

        logger::info(
            "[DualPad][Config] Touchpad mode={} edgeThreshold={:.2f} leftRightBoundary={:.2f} slideThreshold={:.2f}",
            ToString(_touchpadConfig.mode),
            _touchpadConfig.edgeThreshold,
            _touchpadConfig.leftRightBoundary,
            _touchpadConfig.slideThreshold);
        logger::info(
            "[DualPad][Config] Loaded {} bindings from {} ({} standard fallbacks added)",
            bindingCount,
            path.string(),
            fallbackCount);
        return bindingCount > 0 || fallbackCount > 0;
    }
}
