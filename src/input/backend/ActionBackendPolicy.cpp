#include "pch.h"
#include "input/backend/ActionBackendPolicy.h"

#include "input/Action.h"
#include "input/RuntimeConfig.h"
#include "input/backend/ModEventKeyPool.h"

using namespace std::literals;

namespace dualpad::input::backend
{
    namespace
    {
        constexpr bool IsPluginActionId(std::string_view actionId)
        {
            return actionId == actions::ToggleHUD ||
                actionId == actions::Screenshot ||
                false;
        }

        constexpr bool IsKeyboardHelperActionId(std::string_view actionId)
        {
            return actionId.starts_with("VirtualKey."sv) ||
                actionId.starts_with("FKey."sv);
        }

        constexpr bool IsModEventActionId(std::string_view actionId)
        {
            return FindModEventKeySlot(actionId) != nullptr;
        }

        constexpr bool IsComboNativeHotkeyActionId(std::string_view actionId)
        {
            return actionId == actions::Hotkey3 ||
                actionId == actions::Hotkey4 ||
                actionId == actions::Hotkey5 ||
                actionId == actions::Hotkey6 ||
                actionId == actions::Hotkey7 ||
                actionId == actions::Hotkey8;
        }
    }

    ActionRoutingDecision ActionBackendPolicy::Decide(std::string_view actionId)
    {
        if (IsPluginAction(actionId)) {
            return {
                .backend = PlannedBackend::Plugin,
                .kind = PlannedActionKind::PluginAction,
                .contract = ActionOutputContract::Pulse,
                .lifecyclePolicy = ActionLifecyclePolicy::None,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (IsKeyboardHelperActionId(actionId)) {
            return {
                .backend = PlannedBackend::KeyboardHelper,
                .kind = PlannedActionKind::KeyboardKey,
                .contract = ActionOutputContract::Pulse,
                .lifecyclePolicy = ActionLifecyclePolicy::None,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (IsModEventActionId(actionId)) {
            return {
                .backend = PlannedBackend::ModEvent,
                .kind = PlannedActionKind::ModEvent,
                .contract = ActionOutputContract::Pulse,
                .lifecyclePolicy = ActionLifecyclePolicy::None,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (const auto* descriptor = FindNativeActionDescriptor(actionId)) {
            if (IsComboNativeHotkeyActionId(actionId) &&
                !RuntimeConfig::GetSingleton().EnableComboNativeHotkeys3To8()) {
                return {
                    .backend = PlannedBackend::None,
                    .kind = PlannedActionKind::PluginAction,
                    .contract = ActionOutputContract::None,
                    .lifecyclePolicy = ActionLifecyclePolicy::None,
                    .nativeCode = NativeControlCode::None,
                    .ownsLifecycle = false
                };
            }

            return {
                .backend = descriptor->backend,
                .kind = descriptor->kind,
                .contract = descriptor->contract,
                .lifecyclePolicy = descriptor->lifecyclePolicy,
                .nativeCode = descriptor->nativeCode,
                .ownsLifecycle = descriptor->ownsLifecycle
            };
        }

        return {
            .backend = PlannedBackend::None,
            .kind = PlannedActionKind::PluginAction,
            .contract = ActionOutputContract::None,
            .lifecyclePolicy = ActionLifecyclePolicy::None,
            .nativeCode = NativeControlCode::None,
            .ownsLifecycle = false
        };
    }

    bool ActionBackendPolicy::IsPluginAction(std::string_view actionId)
    {
        return IsPluginActionId(actionId);
    }

    bool ActionBackendPolicy::IsLikelyModAction(std::string_view actionId)
    {
        return IsModEventActionId(actionId) || IsKeyboardHelperActionId(actionId);
    }
}
