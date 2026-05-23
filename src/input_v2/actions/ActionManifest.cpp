#include "pch.h"

#include "input_v2/actions/ActionManifest.h"

#include "input/Action.h"
#include "input/IniParseHelpers.h"
#include "input/PadProfile.h"
#include "input/mapping/PadEvent.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/config/LegacyIniImporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <format>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace dualpad::input_v2::actions
{
    namespace
    {
        using dualpad::input::InputContext;
        using dualpad::input::PadAxisId;
        using dualpad::input::PadBits;
        using dualpad::input::Trigger;
        using dualpad::input::TriggerHash;
        using dualpad::input::TriggerType;
        using dualpad::input::TouchpadConfig;

        struct TriggerParseResult
        {
            bool ok{ false };
            std::string message;
            Trigger trigger{};
            std::string controlPath;
            std::string interaction;
            std::vector<std::string> requiredChordPaths;
        };

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

        std::uint32_t GestureNameToCode(std::string_view name)
        {
            using namespace dualpad::input::mapping_codes;
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

        bool ParseButtonList(std::string_view chord, std::vector<std::string>& outNames, std::vector<std::uint32_t>& outCodes)
        {
            outNames.clear();
            outCodes.clear();

            std::size_t tokenStart = 0;
            while (tokenStart <= chord.size()) {
                const auto plusPos = chord.find('+', tokenStart);
                const auto tokenView = plusPos == std::string_view::npos ?
                    chord.substr(tokenStart) :
                    chord.substr(tokenStart, plusPos - tokenStart);

                const auto token = dualpad::input::ini::Trim(std::string(tokenView));
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

                outNames.push_back(token);
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
                return false;
            }

            return true;
        }

        TriggerParseResult ParseTriggerString(std::string_view triggerStr)
        {
            TriggerParseResult result{};

            const auto colonPos = triggerStr.find(':');
            if (colonPos == std::string_view::npos) {
                result.ok = false;
                result.message = "missing ':' in trigger";
                return result;
            }

            const auto typeStr = triggerStr.substr(0, colonPos);
            const auto codeStr = triggerStr.substr(colonPos + 1);

            const auto token = dualpad::input::ini::Trim(std::string(codeStr));
            if (token.empty()) {
                result.ok = false;
                result.message = "empty trigger token";
                return result;
            }

            if (typeStr == "Gesture") {
                result.trigger.type = TriggerType::Gesture;
                result.trigger.modifiers.clear();
                result.trigger.code = GestureNameToCode(token);
                if (result.trigger.code == 0) {
                    result.ok = false;
                    result.message = "unknown gesture token";
                    return result;
                }
                result.controlPath = std::string("TouchGesture/") + token;
                result.interaction = "Gesture";
                result.ok = true;
                return result;
            }

            if (typeStr == "Axis") {
                result.trigger.type = TriggerType::Axis;
                result.trigger.modifiers.clear();
                result.trigger.code = AxisNameToCode(token);
                if (result.trigger.code == 0) {
                    result.ok = false;
                    result.message = "unknown axis token";
                    return result;
                }
                result.controlPath = std::string("Axis/") + token;
                result.interaction = "Value";
                result.ok = true;
                return result;
            }

            if (typeStr == "Button") {
                if (token.find('+') != std::string::npos) {
                    result.ok = false;
                    result.message = "Button:* does not accept modifier chords";
                    return result;
                }

                result.trigger.type = TriggerType::Button;
                result.trigger.modifiers.clear();
                result.trigger.code = ButtonNameToCode(token);
                if (result.trigger.code == 0) {
                    result.ok = false;
                    result.message = "unknown button token";
                    return result;
                }
                result.controlPath = std::string("Button/") + token;
                result.interaction = "Press";
                result.ok = true;
                return result;
            }

            if (typeStr == "Combo") {
                std::vector<std::string> names;
                std::vector<std::uint32_t> codes;
                if (!ParseButtonList(token, names, codes) || codes.size() != 2) {
                    result.ok = false;
                    result.message = "Combo:* requires exactly two digital buttons";
                    return result;
                }

                // Normalize: sort codes and names by code order.
                std::vector<std::pair<std::uint32_t, std::string>> zipped = {
                    { codes[0], names[0] },
                    { codes[1], names[1] },
                };
                (std::sort)(zipped.begin(), zipped.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

                result.trigger.type = TriggerType::Combo;
                result.trigger.code = zipped[1].first;
                result.trigger.modifiers = { zipped[0].first };

                result.controlPath = std::string("Combo/") + zipped[0].second + "+" + zipped[1].second;
                result.interaction = "Combo";
                result.ok = true;
                return result;
            }

            const auto parseChordLike = [&](TriggerType type, std::string_view interactionLabel) -> TriggerParseResult {
                TriggerParseResult chordRes{};
                std::vector<std::string> names;
                std::vector<std::uint32_t> codes;
                if (!ParseButtonList(token, names, codes)) {
                    chordRes.ok = false;
                    chordRes.message = "invalid chord";
                    return chordRes;
                }

                // Primary is the rightmost token, modifiers are unordered.
                chordRes.trigger.type = type;
                chordRes.trigger.code = codes.back();
                chordRes.trigger.modifiers.assign(codes.begin(), codes.end() - 1);
                (std::sort)(chordRes.trigger.modifiers.begin(), chordRes.trigger.modifiers.end());

                const auto primaryName = names.back();
                chordRes.controlPath = std::string("Button/") + primaryName;
                chordRes.interaction = std::string(interactionLabel);
                for (std::size_t i = 0; i + 1 < names.size(); ++i) {
                    chordRes.requiredChordPaths.push_back(std::string("Button/") + names[i]);
                }
                (std::sort)(chordRes.requiredChordPaths.begin(), chordRes.requiredChordPaths.end());

                chordRes.ok = true;
                return chordRes;
            };

            if (typeStr == "Layer") {
                return parseChordLike(TriggerType::Layer, "Layer");
            }

            if (typeStr == "Hold") {
                return parseChordLike(TriggerType::Hold, "Hold");
            }

            if (typeStr == "Tap") {
                return parseChordLike(TriggerType::Tap, "Tap");
            }

            result.ok = false;
            result.message = "unknown trigger type";
            return result;
        }

        std::string NormalizeKey(std::string_view text)
        {
            return dualpad::input::ini::Trim(std::string(text));
        }

        bool IsVisibleForDisplayBinding(const Trigger& trigger)
        {
            switch (trigger.type) {
            case TriggerType::Button:
            case TriggerType::Hold:
            case TriggerType::Tap:
            case TriggerType::Combo:
            case TriggerType::Layer:
                return true;
            default:
                return false;
            }
        }

        bool StartsWith(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        bool IsModEvent(std::string_view actionId)
        {
            if (!StartsWith(actionId, "ModEvent")) {
                return false;
            }
            const auto suffix = actionId.substr(std::string_view("ModEvent").size());
            if (suffix.empty()) {
                return false;
            }
            int value = 0;
            for (char c : suffix) {
                if (c < '0' || c > '9') {
                    return false;
                }
                value = value * 10 + (c - '0');
            }
            return value >= 1 && value <= 24;
        }

        const std::unordered_set<std::string_view>& KnownActionIdSet()
        {
            using namespace dualpad::input;
            using namespace dualpad::input::actions;

            static const std::unordered_set<std::string_view> kKnown = {
                // Gameplay / native actions.
                Jump,
                Attack,
                Block,
                Activate,
                ReadyWeapon,
                TweenMenu,
                Sprint,
                Sneak,
                Shout,
                Favorites,
                Hotkey1,
                Hotkey2,
                Hotkey3,
                Hotkey4,
                Hotkey5,
                Hotkey6,
                Hotkey7,
                Hotkey8,

                // Menu actions.
                MenuConfirm,
                MenuCancel,
                MenuScrollUp,
                MenuScrollDown,
                MenuLeft,
                MenuRight,
                MenuPageUp,
                MenuPageDown,
                MenuSortByName,
                MenuSortByValue,
                MenuDownloadAll,

                // Console.
                ConsoleExecute,
                ConsoleHistoryUp,
                ConsoleHistoryDown,
                ConsolePickPrevious,
                ConsolePickNext,
                ConsoleNextFocus,
                ConsolePreviousFocus,

                // Dialogue.
                DialoguePreviousOption,
                DialogueNextOption,

                // Favorites menu.
                FavoritesPreviousItem,
                FavoritesNextItem,
                FavoritesAccept,
                FavoritesCancel,
                FavoritesUp,
                FavoritesDown,
                FavoritesLeftStick,

                // Item menu.
                ItemLeftEquip,
                ItemRightEquip,
                ItemZoom,
                ItemRotate,
                ItemXButton,
                ItemYButton,

                InventoryChargeItem,

                // Book.
                BookClose,
                BookPreviousPage,
                BookNextPage,

                // Map.
                MapCancel,
                MapLook,
                MapZoomIn,
                MapZoomOut,
                MapClick,
                MapCursor,
                MapOpenJournal,
                MapPlayerPosition,
                MapLocalMap,

                // Stats.
                StatsRotate,

                // Cursor.
                CursorMove,
                CursorClick,

                // Journal.
                JournalXButton,
                JournalYButton,
                JournalTabLeft,
                JournalTabRight,

                // Debug overlay.
                DebugOverlayNextFocus,
                DebugOverlayPreviousFocus,
                DebugOverlayUp,
                DebugOverlayDown,
                DebugOverlayLeft,
                DebugOverlayRight,
                DebugOverlayToggleMinimize,
                DebugOverlayToggleMove,
                DebugOverlayLeftTrigger,
                DebugOverlayRightTrigger,
                DebugOverlayB,
                DebugOverlayY,
                DebugOverlayX,

                // TFC.
                TFCCameraZUp,
                TFCCameraZDown,
                TFCWorldZUp,
                TFCWorldZDown,
                TFCLockToZPlane,

                // Debug map.
                DebugMapLook,
                DebugMapZoomIn,
                DebugMapZoomOut,
                DebugMapMove,

                // Lockpicking.
                LockpickingRotatePick,
                LockpickingRotateLock,
                LockpickingDebugMode,
                LockpickingCancel,

                // Creations menu.
                CreationsAccept,
                CreationsCancel,
                CreationsUp,
                CreationsDown,
                CreationsLeft,
                CreationsRight,
                CreationsOptions,
                CreationsLeftStick,
                CreationsLoadOrderAndDelete,
                CreationsCategorySideBar,
                CreationsLikeUnlike,
                CreationsSearchEdit,
                CreationsFilter,
                CreationsPurchaseCredits,

                FavorCancel,

                // Extended gameplay utility.
                OpenJournal,
                Pause,
                TogglePOV,
                ToggleHUD,
                Screenshot,
                NativeScreenshot,
                Wait,

                // Axis actions that are part of the formal native surface.
                "Game.Move",
                "Game.Look",
                "Game.LeftTrigger",
                "Game.RightTrigger",

                // Legacy generic menu stick.
                "Menu.LeftStick",
            };

            return kKnown;
        }

        ActionValueKind InferActionValueKind(std::string_view actionId)
        {
            if (actionId == "Game.Move" ||
                actionId == "Game.Look" ||
                actionId == dualpad::input::actions::MapLook ||
                actionId == dualpad::input::actions::MapCursor ||
                actionId == dualpad::input::actions::CursorMove ||
                actionId == dualpad::input::actions::DebugMapLook ||
                actionId == dualpad::input::actions::DebugMapMove) {
                return ActionValueKind::Axis2D;
            }
            if (EndsWith(actionId, "Trigger") ||
                EndsWith(actionId, "ZoomIn") ||
                EndsWith(actionId, "ZoomOut") ||
                EndsWith(actionId, "Rotate") ||
                actionId == "Game.LeftTrigger" ||
                actionId == "Game.RightTrigger") {
                return ActionValueKind::Axis1D;
            }
            return ActionValueKind::Digital;
        }

        ActionDomain InferActionDomain(std::string_view actionId)
        {
            if (StartsWith(actionId, "Game.")) {
                if (actionId == dualpad::input::actions::Pause ||
                    actionId == dualpad::input::actions::Screenshot ||
                    actionId == dualpad::input::actions::NativeScreenshot ||
                    actionId == dualpad::input::actions::Wait) {
                    return ActionDomain::Utility;
                }
                return ActionDomain::Gameplay;
            }
            if (StartsWith(actionId, "ModEvent") ||
                StartsWith(actionId, "VirtualKey.") ||
                StartsWith(actionId, "FKey.")) {
                return ActionDomain::Utility;
            }
            return ActionDomain::Menu;
        }

        std::string OutputDescriptorIdFor(std::string_view actionId, ActionValueKind valueKind, ActionDomain domain)
        {
            if (StartsWith(actionId, "ModEvent") ||
                StartsWith(actionId, "VirtualKey.") ||
                StartsWith(actionId, "FKey.")) {
                return "out.keyboard-helper";
            }
            if (valueKind == ActionValueKind::Axis1D || valueKind == ActionValueKind::Axis2D) {
                return "out.native-axis";
            }
            if (domain == ActionDomain::Gameplay) {
                return "out.native-digital";
            }
            if (domain == ActionDomain::Utility) {
                return "out.utility";
            }
            return "out.menu-digital";
        }

        std::string ContractFor(std::string_view actionId, ActionValueKind valueKind, ActionDomain domain)
        {
            if (StartsWith(actionId, "ModEvent") ||
                StartsWith(actionId, "VirtualKey.") ||
                StartsWith(actionId, "FKey.")) {
                return "KeyboardHelperPulse";
            }
            if (valueKind == ActionValueKind::Axis1D || valueKind == ActionValueKind::Axis2D) {
                return "NativeAxis";
            }
            if (domain == ActionDomain::Gameplay) {
                return "NativeDigital";
            }
            if (domain == ActionDomain::Utility) {
                return "UtilityPulse";
            }
            return "MenuDigital";
        }

        ActionDefinition MakeActionDefinition(std::string_view actionId)
        {
            ActionDefinition action{};
            action.id = std::string(actionId);
            action.valueKind = InferActionValueKind(action.id);
            action.domain = InferActionDomain(action.id);
            action.contract = ContractFor(action.id, action.valueKind, action.domain);
            action.outputDescriptorId = OutputDescriptorIdFor(action.id, action.valueKind, action.domain);
            action.promptHintId = std::string("prompt.") + action.id;
            return action;
        }

        std::vector<OutputDescriptor> BuiltInOutputDescriptors()
        {
            return {
                OutputDescriptor{ .id = "out.native-digital", .kind = "NativeDigital", .target = "AuthoritativePollState" },
                OutputDescriptor{ .id = "out.native-axis", .kind = "NativeAxis", .target = "AuthoritativePollState" },
                OutputDescriptor{ .id = "out.menu-digital", .kind = "MenuDigital", .target = "BindingManagerCompat" },
                OutputDescriptor{ .id = "out.keyboard-helper", .kind = "KeyboardHelper", .target = "KeyboardHelperBackend" },
                OutputDescriptor{ .id = "out.utility", .kind = "Utility", .target = "ActionDispatcher" },
            };
        }

        std::vector<ManifestPolicy> BuiltInPolicies()
        {
            return {
                ManifestPolicy{ .id = "displayBinding.ambiguity", .value = "fail-closed" },
                ManifestPolicy{ .id = "legacyBindingProjection.authority", .value = "compiled-manifest-only" },
                ManifestPolicy{ .id = "actionSet.model", .value = "base-set-plus-layer-stack" },
            };
        }

        DisplayBinding MakeDisplayBinding(
            const ProjectedLegacyBinding& projected,
            std::string_view baseSetId,
            std::string_view layerId)
        {
            DisplayBinding display{};
            display.actionId = projected.actionId;
            display.baseSetId = std::string(baseSetId);
            display.layerId = layerId.empty() ? std::nullopt : std::optional<std::string>(std::string(layerId));
            display.deviceFamily = "DualSense";
            display.controlPath =
                std::string(dualpad::input::ToString(projected.trigger.type)) + ":" + std::to_string(projected.trigger.code);
            display.interaction = std::string(dualpad::input::ToString(projected.trigger.type));
            display.legacyTrigger = projected.trigger;
            display.legacyContext = projected.context;
            return display;
        }

        void AddIfMissing(
            std::unordered_map<Trigger, std::string, TriggerHash>& bindings,
            const Trigger& trigger,
            std::string_view actionId,
            std::size_t& inOutAddedCount)
        {
            if (bindings.contains(trigger)) {
                return;
            }
            bindings.emplace(trigger, std::string(actionId));
            ++inOutAddedCount;
        }

        Trigger MakeButtonTrigger(std::uint32_t code)
        {
            Trigger t{};
            t.type = TriggerType::Button;
            t.code = code;
            return t;
        }

        Trigger MakeAxisTrigger(PadAxisId axis)
        {
            Trigger t{};
            t.type = TriggerType::Axis;
            t.code = static_cast<std::uint32_t>(axis);
            return t;
        }

        void ApplyStandardFallbackBindings(
            std::unordered_map<InputContext, std::unordered_map<Trigger, std::string, TriggerHash>>& byContext,
            std::size_t& outAddedCount)
        {
            using namespace dualpad::input;
            using namespace dualpad::input::actions;

            const auto& bits = GetPadBits(GetActivePadProfile());

            static constexpr std::array kGameplayContexts = {
                InputContext::Gameplay,
                InputContext::Combat,
                InputContext::Sneaking,
                InputContext::Riding,
                InputContext::Werewolf,
                InputContext::VampireLord
            };

            static constexpr std::array kGenericMenuContexts = {
                InputContext::Menu,
                InputContext::InventoryMenu,
                InputContext::MagicMenu,
                InputContext::TweenMenu,
                InputContext::ContainerMenu,
                InputContext::BarterMenu,
                InputContext::TrainingMenu,
                InputContext::LevelUpMenu,
                InputContext::RaceSexMenu,
                InputContext::StatsMenu,
                InputContext::SkillMenu,
                InputContext::MessageBoxMenu,
                InputContext::QuantityMenu,
                InputContext::GiftMenu
            };

            const auto addBaseMenuBindings = [&](InputContext context) {
                auto& bindings = byContext[context];
                AddIfMissing(bindings, MakeButtonTrigger(bits.cross), MenuConfirm, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.circle), MenuCancel, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.triangle), MenuDownloadAll, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.dpadUp), MenuScrollUp, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.dpadDown), MenuScrollDown, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.dpadLeft), MenuLeft, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.dpadRight), MenuRight, outAddedCount);
                AddIfMissing(bindings, MakeAxisTrigger(PadAxisId::LeftStickX), "Menu.LeftStick", outAddedCount);
                AddIfMissing(bindings, MakeAxisTrigger(PadAxisId::LeftStickY), "Menu.LeftStick", outAddedCount);
            };

            for (const auto context : kGameplayContexts) {
                auto& bindings = byContext[context];
                AddIfMissing(bindings, MakeButtonTrigger(bits.cross), Activate, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.square), ReadyWeapon, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.circle), TweenMenu, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.triangle), Jump, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.l1), Sprint, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.create), Wait, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.l3), Sneak, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.options), OpenJournal, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.r1), Shout, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.r3), TogglePOV, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.dpadLeft), Hotkey1, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.dpadRight), Hotkey2, outAddedCount);
                AddIfMissing(bindings, MakeButtonTrigger(bits.dpadUp), Favorites, outAddedCount);
                AddIfMissing(bindings, MakeAxisTrigger(PadAxisId::LeftStickX), "Game.Move", outAddedCount);
                AddIfMissing(bindings, MakeAxisTrigger(PadAxisId::LeftStickY), "Game.Move", outAddedCount);
                AddIfMissing(bindings, MakeAxisTrigger(PadAxisId::RightStickX), "Game.Look", outAddedCount);
                AddIfMissing(bindings, MakeAxisTrigger(PadAxisId::RightStickY), "Game.Look", outAddedCount);
                AddIfMissing(bindings, MakeAxisTrigger(PadAxisId::LeftTrigger), "Game.LeftTrigger", outAddedCount);
                AddIfMissing(bindings, MakeAxisTrigger(PadAxisId::RightTrigger), "Game.RightTrigger", outAddedCount);
            }

            for (const auto context : kGenericMenuContexts) {
                addBaseMenuBindings(context);
            }

            AddIfMissing(byContext[InputContext::InventoryMenu], MakeButtonTrigger(bits.r1), InventoryChargeItem, outAddedCount);

            addBaseMenuBindings(InputContext::DialogueMenu);
            AddIfMissing(byContext[InputContext::DialogueMenu], MakeButtonTrigger(bits.dpadUp), DialoguePreviousOption, outAddedCount);
            AddIfMissing(byContext[InputContext::DialogueMenu], MakeButtonTrigger(bits.dpadDown), DialogueNextOption, outAddedCount);

            addBaseMenuBindings(InputContext::FavoritesMenu);
            AddIfMissing(byContext[InputContext::FavoritesMenu], MakeButtonTrigger(bits.cross), FavoritesAccept, outAddedCount);
            AddIfMissing(byContext[InputContext::FavoritesMenu], MakeButtonTrigger(bits.circle), FavoritesCancel, outAddedCount);
            AddIfMissing(byContext[InputContext::FavoritesMenu], MakeButtonTrigger(bits.dpadUp), FavoritesUp, outAddedCount);
            AddIfMissing(byContext[InputContext::FavoritesMenu], MakeButtonTrigger(bits.dpadDown), FavoritesDown, outAddedCount);
            AddIfMissing(byContext[InputContext::FavoritesMenu], MakeAxisTrigger(PadAxisId::LeftStickX), FavoritesLeftStick, outAddedCount);
            AddIfMissing(byContext[InputContext::FavoritesMenu], MakeAxisTrigger(PadAxisId::LeftStickY), FavoritesLeftStick, outAddedCount);

            addBaseMenuBindings(InputContext::JournalMenu);
            AddIfMissing(byContext[InputContext::JournalMenu], MakeButtonTrigger(bits.square), JournalXButton, outAddedCount);
            AddIfMissing(byContext[InputContext::JournalMenu], MakeButtonTrigger(bits.triangle), JournalYButton, outAddedCount);
            AddIfMissing(byContext[InputContext::JournalMenu], MakeAxisTrigger(PadAxisId::LeftTrigger), JournalTabLeft, outAddedCount);
            AddIfMissing(byContext[InputContext::JournalMenu], MakeAxisTrigger(PadAxisId::RightTrigger), JournalTabRight, outAddedCount);

            addBaseMenuBindings(InputContext::BookMenu);
            AddIfMissing(byContext[InputContext::BookMenu], MakeButtonTrigger(bits.circle), BookClose, outAddedCount);
            AddIfMissing(byContext[InputContext::BookMenu], MakeButtonTrigger(bits.dpadLeft), BookPreviousPage, outAddedCount);
            AddIfMissing(byContext[InputContext::BookMenu], MakeButtonTrigger(bits.dpadRight), BookNextPage, outAddedCount);

            addBaseMenuBindings(InputContext::Book);
            AddIfMissing(byContext[InputContext::Book], MakeButtonTrigger(bits.circle), BookClose, outAddedCount);
            AddIfMissing(byContext[InputContext::Book], MakeButtonTrigger(bits.dpadLeft), BookPreviousPage, outAddedCount);
            AddIfMissing(byContext[InputContext::Book], MakeButtonTrigger(bits.dpadRight), BookNextPage, outAddedCount);

            AddIfMissing(byContext[InputContext::MapMenu], MakeButtonTrigger(bits.cross), MapClick, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeButtonTrigger(bits.circle), MapCancel, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeButtonTrigger(bits.square), MapLocalMap, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeButtonTrigger(bits.triangle), MapPlayerPosition, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeButtonTrigger(bits.dpadLeft), MapOpenJournal, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeAxisTrigger(PadAxisId::LeftStickX), MapCursor, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeAxisTrigger(PadAxisId::LeftStickY), MapCursor, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeAxisTrigger(PadAxisId::RightStickX), MapLook, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeAxisTrigger(PadAxisId::RightStickY), MapLook, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeAxisTrigger(PadAxisId::LeftTrigger), MapZoomOut, outAddedCount);
            AddIfMissing(byContext[InputContext::MapMenu], MakeAxisTrigger(PadAxisId::RightTrigger), MapZoomIn, outAddedCount);

            AddIfMissing(byContext[InputContext::Console], MakeButtonTrigger(bits.cross), ConsoleExecute, outAddedCount);
            AddIfMissing(byContext[InputContext::Console], MakeButtonTrigger(bits.circle), MenuCancel, outAddedCount);
            AddIfMissing(byContext[InputContext::Console], MakeButtonTrigger(bits.dpadUp), ConsolePickNext, outAddedCount);
            AddIfMissing(byContext[InputContext::Console], MakeButtonTrigger(bits.dpadDown), ConsolePickPrevious, outAddedCount);
            AddIfMissing(byContext[InputContext::Console], MakeButtonTrigger(bits.l1), ConsolePreviousFocus, outAddedCount);
            AddIfMissing(byContext[InputContext::Console], MakeButtonTrigger(bits.r1), ConsoleNextFocus, outAddedCount);

            AddIfMissing(byContext[InputContext::ItemMenu], MakeAxisTrigger(PadAxisId::LeftTrigger), ItemLeftEquip, outAddedCount);
            AddIfMissing(byContext[InputContext::ItemMenu], MakeAxisTrigger(PadAxisId::RightTrigger), ItemRightEquip, outAddedCount);
            AddIfMissing(byContext[InputContext::ItemMenu], MakeButtonTrigger(bits.r3), ItemZoom, outAddedCount);
            AddIfMissing(byContext[InputContext::ItemMenu], MakeButtonTrigger(bits.square), ItemXButton, outAddedCount);
            AddIfMissing(byContext[InputContext::ItemMenu], MakeButtonTrigger(bits.triangle), ItemYButton, outAddedCount);
            AddIfMissing(byContext[InputContext::ItemMenu], MakeAxisTrigger(PadAxisId::RightStickX), ItemRotate, outAddedCount);
            AddIfMissing(byContext[InputContext::ItemMenu], MakeAxisTrigger(PadAxisId::RightStickY), ItemRotate, outAddedCount);

            AddIfMissing(byContext[InputContext::Stats], MakeAxisTrigger(PadAxisId::LeftStickX), StatsRotate, outAddedCount);
            AddIfMissing(byContext[InputContext::Stats], MakeAxisTrigger(PadAxisId::LeftStickY), StatsRotate, outAddedCount);

            AddIfMissing(byContext[InputContext::Cursor], MakeAxisTrigger(PadAxisId::RightStickX), CursorMove, outAddedCount);
            AddIfMissing(byContext[InputContext::Cursor], MakeAxisTrigger(PadAxisId::RightStickY), CursorMove, outAddedCount);
            AddIfMissing(byContext[InputContext::Cursor], MakeButtonTrigger(bits.cross), CursorClick, outAddedCount);

            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.l1), DebugOverlayPreviousFocus, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.r1), DebugOverlayNextFocus, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.dpadUp), DebugOverlayUp, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.dpadDown), DebugOverlayDown, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.dpadLeft), DebugOverlayLeft, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.dpadRight), DebugOverlayRight, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.create), DebugOverlayToggleMinimize, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.r3), DebugOverlayToggleMove, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.circle), DebugOverlayB, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.triangle), DebugOverlayY, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeButtonTrigger(bits.square), DebugOverlayX, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeAxisTrigger(PadAxisId::LeftTrigger), DebugOverlayLeftTrigger, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugOverlay], MakeAxisTrigger(PadAxisId::RightTrigger), DebugOverlayRightTrigger, outAddedCount);

            AddIfMissing(byContext[InputContext::TFCMode], MakeButtonTrigger(bits.square), TFCLockToZPlane, outAddedCount);
            AddIfMissing(byContext[InputContext::TFCMode], MakeButtonTrigger(bits.l1), TFCWorldZDown, outAddedCount);
            AddIfMissing(byContext[InputContext::TFCMode], MakeButtonTrigger(bits.r1), TFCWorldZUp, outAddedCount);
            AddIfMissing(byContext[InputContext::TFCMode], MakeAxisTrigger(PadAxisId::LeftTrigger), TFCCameraZDown, outAddedCount);
            AddIfMissing(byContext[InputContext::TFCMode], MakeAxisTrigger(PadAxisId::RightTrigger), TFCCameraZUp, outAddedCount);

            AddIfMissing(byContext[InputContext::DebugMapMenu], MakeAxisTrigger(PadAxisId::LeftStickX), DebugMapMove, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugMapMenu], MakeAxisTrigger(PadAxisId::LeftStickY), DebugMapMove, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugMapMenu], MakeAxisTrigger(PadAxisId::RightStickX), DebugMapLook, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugMapMenu], MakeAxisTrigger(PadAxisId::RightStickY), DebugMapLook, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugMapMenu], MakeAxisTrigger(PadAxisId::LeftTrigger), DebugMapZoomOut, outAddedCount);
            AddIfMissing(byContext[InputContext::DebugMapMenu], MakeAxisTrigger(PadAxisId::RightTrigger), DebugMapZoomIn, outAddedCount);

            AddIfMissing(byContext[InputContext::Lockpicking], MakeButtonTrigger(bits.circle), LockpickingCancel, outAddedCount);
            AddIfMissing(byContext[InputContext::Lockpicking], MakeButtonTrigger(bits.square), LockpickingDebugMode, outAddedCount);
            AddIfMissing(byContext[InputContext::Lockpicking], MakeAxisTrigger(PadAxisId::LeftStickX), LockpickingRotatePick, outAddedCount);
            AddIfMissing(byContext[InputContext::Lockpicking], MakeAxisTrigger(PadAxisId::LeftStickY), LockpickingRotatePick, outAddedCount);
            AddIfMissing(byContext[InputContext::Lockpicking], MakeAxisTrigger(PadAxisId::RightStickX), LockpickingRotateLock, outAddedCount);
            AddIfMissing(byContext[InputContext::Lockpicking], MakeAxisTrigger(PadAxisId::RightStickY), LockpickingRotateLock, outAddedCount);

            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.cross), CreationsAccept, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.circle), CreationsCancel, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.dpadUp), CreationsUp, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.dpadDown), CreationsDown, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.dpadLeft), CreationsLeft, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.dpadRight), CreationsRight, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.options), CreationsOptions, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.triangle), CreationsLoadOrderAndDelete, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.r1), CreationsLikeUnlike, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.l1), CreationsSearchEdit, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeButtonTrigger(bits.square), CreationsPurchaseCredits, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeAxisTrigger(PadAxisId::LeftStickX), CreationsLeftStick, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeAxisTrigger(PadAxisId::LeftStickY), CreationsLeftStick, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeAxisTrigger(PadAxisId::LeftTrigger), CreationsCategorySideBar, outAddedCount);
            AddIfMissing(byContext[InputContext::CreationsMenu], MakeAxisTrigger(PadAxisId::RightTrigger), CreationsFilter, outAddedCount);

            AddIfMissing(byContext[InputContext::Favor], MakeButtonTrigger(bits.circle), FavorCancel, outAddedCount);
        }

        std::optional<std::string> FindActionForTrigger(
            const std::unordered_map<Trigger, std::string, TriggerHash>& bindings,
            const Trigger& trigger)
        {
            const auto it = bindings.find(trigger);
            if (it == bindings.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        bool IsBaseSectionName(std::string_view canonicalContextName)
        {
            // Phase 1: treat [Gameplay] and [Menu] as base bindings (layerId=nullopt).
            return canonicalContextName == "Gameplay" || canonicalContextName == "Menu";
        }

        bool ParseTouchpadSetting(std::string_view key, std::string_view value, TouchpadConfig& inOutConfig)
        {
            if (key == "Mode") {
                if (value == "LeftCenterRight" || value == "LCR") {
                    inOutConfig.mode = dualpad::input::TouchpadMode::LeftCenterRight;
                    return true;
                }
                if (value == "Edge") {
                    inOutConfig.mode = dualpad::input::TouchpadMode::Edge;
                    return true;
                }
                if (value == "Whole") {
                    inOutConfig.mode = dualpad::input::TouchpadMode::Whole;
                    return true;
                }
                if (value == "Disabled") {
                    inOutConfig.mode = dualpad::input::TouchpadMode::Disabled;
                    return true;
                }
                return false;
            }

            float floatValue = 0.0f;
            if (!ParseFloat(value, floatValue)) {
                return false;
            }

            if (key == "EdgeThreshold") {
                inOutConfig.edgeThreshold = floatValue;
                return true;
            }
            if (key == "LeftRightBoundary") {
                inOutConfig.leftRightBoundary = floatValue;
                return true;
            }
            if (key == "SlideThreshold") {
                inOutConfig.slideThreshold = floatValue;
                return true;
            }

            return false;
        }
    }

    bool ActionManifest::IsKnownActionId(std::string_view actionId)
    {
        const auto key = NormalizeKey(actionId);
        if (key.empty()) {
            return false;
        }

        if (IsModEvent(key)) {
            return true;
        }

        if (StartsWith(key, "VirtualKey.") || StartsWith(key, "FKey.")) {
            return true;
        }

        const auto& known = KnownActionIdSet();
        return known.contains(key);
    }

    ActionManifestCompileResult ActionManifest::Compile(
        const dualpad::input_v2::context::CompiledContextCatalog& compiledCatalog,
        const dualpad::input_v2::config::LegacyBindingsAst& importedBindings,
        std::uint64_t manifestEpoch)
    {
        ActionManifestCompileResult result{};
        result.manifest.manifestEpoch = manifestEpoch;
        result.manifest.legacyBindingProjection.manifestEpoch = manifestEpoch;
        result.manifest.outputDescriptors = BuiltInOutputDescriptors();
        result.manifest.policies = BuiltInPolicies();

        // Freeze base set registry for Phase 1.
        result.manifest.actionSets = { "GameplayBase", "MenuBase" };

        // Build a minimal layer registry from catalog-declared layers.
        {
            std::unordered_set<std::string> layers;
            for (const auto& entry : compiledCatalog.entries) {
                for (const auto& layer : entry.defaultLayerIds) {
                    if (!layer.empty()) {
                        layers.insert(layer);
                    }
                }
            }
            result.manifest.actionLayers.assign(layers.begin(), layers.end());
            (std::sort)(result.manifest.actionLayers.begin(), result.manifest.actionLayers.end());
        }

        // Per-context binding maps for legacy projection, prior to materialization.
        std::unordered_map<InputContext, std::unordered_map<Trigger, std::string, TriggerHash>> bindingsByContext;

        // Per-context inheritance (legacy).
        std::unordered_map<InputContext, InputContext> inheritMap;

        // Touchpad config is compiled into the manifest, not held by BindingConfig.
        TouchpadConfig touchpadConfig{};

        // Parse sections.
        for (const auto& section : importedBindings.sections) {
            const auto sectionName = NormalizeKey(section.name);
            if (sectionName.empty()) {
                continue;
            }

            if (sectionName == "Touchpad") {
                for (const auto& kv : section.entries) {
                    const auto key = NormalizeKey(kv.key);
                    const auto value = NormalizeKey(kv.value);
                    if (key.empty() || value.empty()) {
                        continue;
                    }
                    (void)ParseTouchpadSetting(key, value, touchpadConfig);
                }
                continue;
            }

            const auto ctxId = dualpad::input_v2::context::ContextCatalog::ResolveAlias(compiledCatalog, sectionName);
            if (!ctxId) {
                result.ok = false;
                result.message = std::format("unknown context section [{}]", sectionName);
                return result;
            }

            const auto legacyContextOpt = dualpad::input_v2::context::ContextCatalog::ToLegacyInputContext(compiledCatalog, *ctxId);
            if (!legacyContextOpt) {
                result.ok = false;
                result.message = std::format("context [{}] does not map to legacy InputContext", sectionName);
                return result;
            }

            const auto legacyContext = *legacyContextOpt;
            auto& contextBindings = bindingsByContext[legacyContext];

            for (const auto& kv : section.entries) {
                const auto key = NormalizeKey(kv.key);
                const auto value = NormalizeKey(kv.value);
                if (key.empty() || value.empty()) {
                    continue;
                }

                if (key == "Inherit") {
                    const auto parentId = dualpad::input_v2::context::ContextCatalog::ResolveAlias(compiledCatalog, value);
                    if (!parentId) {
                        result.ok = false;
                        result.message = std::format("invalid inherit rule: [{}] Inherit={}", sectionName, value);
                        return result;
                    }
                    const auto parentLegacy = dualpad::input_v2::context::ContextCatalog::ToLegacyInputContext(compiledCatalog, *parentId);
                    if (!parentLegacy) {
                        result.ok = false;
                        result.message = std::format("inherit target [{}] does not map to legacy InputContext", value);
                        return result;
                    }
                    inheritMap.emplace(legacyContext, *parentLegacy);
                    continue;
                }

                const auto parsed = ParseTriggerString(key);
                if (!parsed.ok) {
                    result.ok = false;
                    result.message = std::format("invalid trigger '{}' in [{}]: {}", key, sectionName, parsed.message);
                    return result;
                }

                if (!IsKnownActionId(value)) {
                    result.ok = false;
                    result.message = std::format("unknown action '{}' in [{}]", value, sectionName);
                    return result;
                }

                if (contextBindings.contains(parsed.trigger)) {
                    // Phase 1 is fail-closed on duplicate normalized binding keys.
                    const auto existing = FindActionForTrigger(contextBindings, parsed.trigger);
                    result.ok = false;
                    result.message = std::format(
                        "duplicate binding key '{}' in [{}] (existing action='{}', new action='{}')",
                        key,
                        sectionName,
                        existing.value_or(""),
                        value);
                    return result;
                }

                contextBindings.emplace(parsed.trigger, value);
            }
        }

        // Apply legacy inheritance at compile-time (child overrides parent).
        std::unordered_map<InputContext, std::uint8_t> visitState;
        std::function<void(InputContext)> applyInheritance = [&](InputContext child) {
            const auto state = visitState[child];
            if (state == 2) {
                return;
            }
            if (state == 1) {
                throw std::runtime_error(std::format("inheritance cycle detected at context {}", dualpad::input::ToString(child)));
            }

            visitState[child] = 1;
            const auto it = inheritMap.find(child);
            if (it != inheritMap.end()) {
                const auto parent = it->second;
                applyInheritance(parent);

                const auto parentIt = bindingsByContext.find(parent);
                if (parentIt != bindingsByContext.end()) {
                    auto& childMap = bindingsByContext[child];
                    for (const auto& [trigger, actionId] : parentIt->second) {
                        if (!childMap.contains(trigger)) {
                            childMap.emplace(trigger, actionId);
                        }
                    }
                }
            }
            visitState[child] = 2;
        };

        try {
            for (const auto& [child, parent] : inheritMap) {
                (void)parent;
                applyInheritance(child);
            }
        } catch (const std::exception& e) {
            result.ok = false;
            result.message = e.what();
            return result;
        }

        // Phase 1: the standard fallback set is part of the compiled manifest output, not a runtime side-effect.
        std::size_t fallbackAdded = 0;
        ApplyStandardFallbackBindings(bindingsByContext, fallbackAdded);

        // Materialize compiled bindings + legacy projection.
        for (const auto& [ctx, bindingMap] : bindingsByContext) {
            // Resolve catalog entry and base/layer ownership.
            const auto ctxName = std::string(dualpad::input::ToString(ctx));
            const auto ctxId = dualpad::input_v2::context::ContextCatalog::ResolveAlias(compiledCatalog, ctxName);
            if (!ctxId) {
                // Some legacy contexts are intentionally not part of the catalog seed (should not happen in Phase 1).
                result.ok = false;
                result.message = std::format("legacy context {} not present in compiled catalog", ctxName);
                return result;
            }

            const auto* entry = dualpad::input_v2::context::ContextCatalog::FindById(compiledCatalog, *ctxId);
            if (!entry || !entry->defaultActionSetId) {
                result.ok = false;
                result.message = std::format("catalog entry {} missing defaultActionSetId", ctxName);
                return result;
            }

            const auto baseSetId = *entry->defaultActionSetId;
            const std::optional<std::string> layerId =
                (!IsBaseSectionName(entry->canonicalContextName) && !entry->defaultLayerIds.empty()) ?
                std::optional<std::string>(entry->defaultLayerIds.back()) :
                std::nullopt;

            for (const auto& [trigger, actionId] : bindingMap) {
                CompiledBinding binding{};
                binding.actionId = actionId;
                binding.baseSetId = baseSetId;
                binding.layerId = layerId;
                binding.deviceFamily = "DualSense";
                binding.legacyTrigger = trigger;
                binding.legacyContext = ctx;

                // Best-effort controlPath/interaction for Phase 1 tests and diagnostics.
                binding.controlPath = std::string(dualpad::input::ToString(trigger.type)) + ":" + std::to_string(trigger.code);
                binding.interaction = std::string(dualpad::input::ToString(trigger.type));
                if (trigger.type == TriggerType::Combo || trigger.type == TriggerType::Layer) {
                    // Populate chord requirements for validation/tracing.
                    for (const auto mod : trigger.modifiers) {
                        binding.requiredChordPaths.push_back("ButtonCode:" + std::to_string(mod));
                    }
                    (std::sort)(binding.requiredChordPaths.begin(), binding.requiredChordPaths.end());
                }

                result.manifest.bindings.push_back(binding);

                ProjectedLegacyBinding projected{};
                projected.context = ctx;
                projected.trigger = trigger;
                projected.actionId = actionId;
                result.manifest.legacyBindingProjection.bindings.push_back(std::move(projected));
            }
        }

        // Compile display bindings (fail-closed on ambiguous visible bindings in the same baseSetId+layerId).
        {
            struct GroupKey
            {
                std::string baseSetId;
                std::string layerId; // empty => nullopt

                bool operator==(const GroupKey& other) const noexcept
                {
                    return baseSetId == other.baseSetId && layerId == other.layerId;
                }
            };

            struct GroupKeyHash
            {
                std::size_t operator()(const GroupKey& key) const noexcept
                {
                    std::size_t h = std::hash<std::string>{}(key.baseSetId);
                    h ^= std::hash<std::string>{}(key.layerId) << 1;
                    return h;
                }
            };

            std::unordered_map<GroupKey, std::unordered_map<std::string, std::vector<ProjectedLegacyBinding>>, GroupKeyHash> candidates;

            for (const auto& b : result.manifest.legacyBindingProjection.bindings) {
                if (!IsVisibleForDisplayBinding(b.trigger)) {
                    continue;
                }

                // Find grouping by resolving catalog entry again.
                const auto ctxName = std::string(dualpad::input::ToString(b.context));
                const auto ctxId = dualpad::input_v2::context::ContextCatalog::ResolveAlias(compiledCatalog, ctxName);
                const auto* entry = (ctxId) ? dualpad::input_v2::context::ContextCatalog::FindById(compiledCatalog, *ctxId) : nullptr;
                if (!entry || !entry->defaultActionSetId) {
                    continue;
                }

                GroupKey key{ *entry->defaultActionSetId, "" };
                if (!IsBaseSectionName(entry->canonicalContextName) && !entry->defaultLayerIds.empty()) {
                    key.layerId = entry->defaultLayerIds.back();
                }

                candidates[key][b.actionId].push_back(b);
            }

            const auto& bits = dualpad::input::GetPadBits(dualpad::input::GetActivePadProfile());
            const auto preferredDisplayButtonCode = [&](std::string_view actionId) -> std::optional<std::uint32_t> {
                using namespace dualpad::input::actions;
                if (actionId == MenuLeft) return bits.dpadLeft;
                if (actionId == MenuRight) return bits.dpadRight;
                if (actionId == MenuScrollUp) return bits.dpadUp;
                if (actionId == MenuScrollDown) return bits.dpadDown;
                return std::nullopt;
            };

            for (auto& [group, byAction] : candidates) {
                for (auto& [actionId, list] : byAction) {
                    if (list.empty()) {
                        continue;
                    }

                    // In Phase 1, display bindings are owned by baseSetId + optional layerId.
                    // Multiple legacy contexts may collapse to the same base+layer (e.g. BookMenu + Book alias).
                    //
                    // Fail-closed only when we have multiple *distinct* visible triggers for the same action
                    // within the same base+layer group. Duplicate entries that differ only by legacy context
                    // (but share the same trigger) are not ambiguous.
                    bool allSameTrigger = true;
                    for (std::size_t i = 1; i < list.size(); ++i) {
                        if (!(list[i].trigger == list[0].trigger)) {
                            allSameTrigger = false;
                            break;
                        }
                    }

                    if (!allSameTrigger) {
                        // Some actions have an explicit canonical display trigger even when multiple bindings exist.
                        // This is treated as a "priority source" for Phase 1 display binding compilation.
                        if (const auto preferredCode = preferredDisplayButtonCode(actionId)) {
                            std::optional<ProjectedLegacyBinding> preferred;
                            for (const auto& candidate : list) {
                                if (candidate.trigger.type != TriggerType::Button) {
                                    continue;
                                }
                                if (candidate.trigger.code != *preferredCode) {
                                    continue;
                                }
                                if (preferred.has_value() && !(preferred->trigger == candidate.trigger)) {
                                    // Should be impossible (duplicate binding key is fail-closed elsewhere), but keep strict.
                                    preferred.reset();
                                    break;
                                }
                                preferred = candidate;
                            }

                            if (preferred.has_value()) {
                                result.manifest.legacyBindingProjection.displayBindings.push_back(*preferred);
                                result.manifest.displayBindings.push_back(
                                    MakeDisplayBinding(*preferred, group.baseSetId, group.layerId));
                                continue;
                            }
                        }

                        result.ok = false;
                        result.message = std::format(
                            "ambiguous visible display binding for action '{}' in group baseSet='{}' layer='{}' (count={})",
                            actionId,
                            group.baseSetId,
                            group.layerId.empty() ? "<none>" : group.layerId,
                            list.size());
                        return result;
                    }

                    // Canonicalize duplicates by keeping a single representative for this base+layer group.
                    result.manifest.legacyBindingProjection.displayBindings.push_back(list[0]);
                    result.manifest.displayBindings.push_back(
                        MakeDisplayBinding(list[0], group.baseSetId, group.layerId));
                }
            }
        }

        {
            std::unordered_set<std::string> actionIds;
            for (const auto actionId : KnownActionIdSet()) {
                actionIds.insert(std::string(actionId));
            }
            for (const auto& binding : result.manifest.bindings) {
                actionIds.insert(binding.actionId);
            }

            std::vector<std::string> sortedActionIds(actionIds.begin(), actionIds.end());
            (std::sort)(sortedActionIds.begin(), sortedActionIds.end());

            result.manifest.actions.reserve(sortedActionIds.size());
            for (const auto& actionId : sortedActionIds) {
                result.manifest.actions.push_back(MakeActionDefinition(actionId));
            }
        }

        result.manifest.touchpadConfig = touchpadConfig;
        result.manifest.legacyBindingProjection.touchpadConfig = result.manifest.touchpadConfig;
        result.ok = true;
        result.message = "ok";
        return result;
    }
}
