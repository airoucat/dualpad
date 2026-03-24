#pragma once

#include "input/PadProfile.h"
#include "input/backend/ActionLifecyclePolicy.h"
#include "input/backend/ActionOutputContract.h"
#include "input/backend/FrameActionPlan.h"
#include "input/backend/NativeControlCode.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    enum class NativeAxisTarget : std::uint8_t
    {
        None = 0,
        MoveStick,
        LookStick,
        LeftTrigger,
        RightTrigger
    };

    enum VirtualPadButtonRoleMask : std::uint16_t
    {
        VirtualPadButtonRoleNone = 0,
        VirtualPadButtonRoleCross = 1u << 0,
        VirtualPadButtonRoleCircle = 1u << 1,
        VirtualPadButtonRoleSquare = 1u << 2,
        VirtualPadButtonRoleTriangle = 1u << 3,
        VirtualPadButtonRoleL1 = 1u << 4,
        VirtualPadButtonRoleR1 = 1u << 5,
        VirtualPadButtonRoleL3 = 1u << 6,
        VirtualPadButtonRoleR3 = 1u << 7,
        VirtualPadButtonRoleCreate = 1u << 8,
        VirtualPadButtonRoleOptions = 1u << 9,
        VirtualPadButtonRoleDpadUp = 1u << 10,
        VirtualPadButtonRoleDpadDown = 1u << 11,
        VirtualPadButtonRoleDpadLeft = 1u << 12,
        VirtualPadButtonRoleDpadRight = 1u << 13
    };

    struct NativeActionDescriptor
    {
        std::string_view actionId{};
        NativeControlCode nativeCode{ NativeControlCode::None };
        PlannedBackend backend{ PlannedBackend::None };
        PlannedActionKind kind{ PlannedActionKind::PluginAction };
        ActionOutputContract contract{ ActionOutputContract::None };
        ActionLifecyclePolicy lifecyclePolicy{ ActionLifecyclePolicy::None };
        bool ownsLifecycle{ false };
        NativeAxisTarget axisTarget{ NativeAxisTarget::None };
        std::uint16_t virtualButtonRoles{ VirtualPadButtonRoleNone };
    };

    const NativeActionDescriptor* FindNativeActionDescriptor(std::string_view actionId);
    const NativeActionDescriptor* FindNativeActionDescriptor(NativeControlCode nativeCode);
    std::uint32_t ResolveVirtualPadBitMask(std::uint16_t virtualButtonRoles, const PadBits& bits);
    std::uint32_t ResolveVirtualPadBitMask(NativeControlCode nativeCode, const PadBits& bits);
}
