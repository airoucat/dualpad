#pragma once

#include "input/Action.h"
#include "input/backend/FrameActionPlan.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    inline constexpr std::uint32_t kNativeDefaultPulseMinDownMs = 40;
    inline constexpr std::uint32_t kNativeDefaultRepeatDelayMs = 350;
    inline constexpr std::uint32_t kNativeDefaultRepeatIntervalMs = 75;

    inline constexpr NativeDigitalPolicyKind ResolveNativeDigitalPolicy(
        PlannedBackend backend,
        PlannedActionKind kind,
        ActionOutputContract contract,
        ActionLifecyclePolicy lifecyclePolicy) noexcept
    {
        if (backend != PlannedBackend::NativeButtonCommit ||
            kind != PlannedActionKind::NativeButton) {
            return NativeDigitalPolicyKind::None;
        }

        switch (contract) {
        case ActionOutputContract::Hold:
            return NativeDigitalPolicyKind::HoldOwner;
        case ActionOutputContract::Repeat:
            return NativeDigitalPolicyKind::RepeatOwner;
        case ActionOutputContract::Toggle:
            return NativeDigitalPolicyKind::ToggleDebounced;
        case ActionOutputContract::Pulse:
            return lifecyclePolicy == ActionLifecyclePolicy::MinDownWindowPulse ?
                NativeDigitalPolicyKind::PulseMinDown :
                NativeDigitalPolicyKind::DeferredPulse;
        case ActionOutputContract::Axis:
        case ActionOutputContract::None:
        default:
            return NativeDigitalPolicyKind::None;
        }
    }

    inline constexpr bool IsNativeDigitalGateAwareAction(
        std::string_view actionId,
        NativeDigitalPolicyKind policy) noexcept
    {
        if (policy == NativeDigitalPolicyKind::None) {
            return false;
        }

        return actionId == actions::Jump ||
            actionId == actions::Activate ||
            actionId == actions::Sprint;
    }

    inline constexpr std::uint32_t ResolveNativeMinDownMs(NativeDigitalPolicyKind policy) noexcept
    {
        return policy == NativeDigitalPolicyKind::PulseMinDown ? kNativeDefaultPulseMinDownMs : 0;
    }

    inline constexpr std::uint32_t ResolveNativeRepeatDelayMs(NativeDigitalPolicyKind policy) noexcept
    {
        return policy == NativeDigitalPolicyKind::RepeatOwner ? kNativeDefaultRepeatDelayMs : 0;
    }

    inline constexpr std::uint32_t ResolveNativeRepeatIntervalMs(NativeDigitalPolicyKind policy) noexcept
    {
        return policy == NativeDigitalPolicyKind::RepeatOwner ? kNativeDefaultRepeatIntervalMs : 0;
    }
}
