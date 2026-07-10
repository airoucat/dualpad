#pragma once

#include "input/backend/ActionLifecyclePolicy.h"
#include "input/backend/NativeActionDescriptor.h"
#include "input/backend/ActionOutputContract.h"
#include "input/backend/FrameActionPlan.h"
#include "input/backend/NativeControlCode.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    enum class ActionRoutingReason : std::uint8_t
    {
        Selected = 0,
        NativeFavoritesDisabled,
        ComboNativeHotkeyDisabled,
        UnknownAction
    };

    struct ActionRoutingDecision
    {
        PlannedBackend backend{ PlannedBackend::None };
        PlannedActionKind kind{ PlannedActionKind::NativeButton };
        ActionOutputContract contract{ ActionOutputContract::Pulse };
        ActionLifecyclePolicy lifecyclePolicy{ ActionLifecyclePolicy::None };
        NativeControlCode nativeCode{ NativeControlCode::None };
        bool ownsLifecycle{ false };
        ActionRoutingReason reason{ ActionRoutingReason::Selected };
    };

    inline constexpr std::string_view ToString(ActionRoutingReason reason) noexcept
    {
        switch (reason) {
        case ActionRoutingReason::NativeFavoritesDisabled:
            return "native_favorites_disabled";
        case ActionRoutingReason::ComboNativeHotkeyDisabled:
            return "combo_native_hotkey_disabled";
        case ActionRoutingReason::UnknownAction:
            return "unknown_action";
        case ActionRoutingReason::Selected:
        default:
            return "selected";
        }
    }

    class ActionBackendPolicy
    {
    public:
        static ActionRoutingDecision Decide(std::string_view actionId);
        static bool IsPluginAction(std::string_view actionId);
        static bool IsLikelyModAction(std::string_view actionId);
    };
}
