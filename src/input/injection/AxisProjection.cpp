#include "pch.h"
#include "input/injection/AxisProjection.h"

#include "input/BindingManager.h"
#include "input/Trigger.h"
#include "input/backend/NativeActionDescriptor.h"

#include <optional>

namespace dualpad::input
{
    namespace
    {
        std::optional<std::string> ResolveAxisAction(PadAxisId axis, InputContext context)
        {
            Trigger trigger{};
            trigger.type = TriggerType::Axis;
            trigger.code = static_cast<std::uint32_t>(axis);
            return BindingManager::GetSingleton().GetActionForTrigger(trigger, context);
        }

        void ApplyAxisBinding(
            ProjectedAnalogState& analog,
            PadAxisId axis,
            float value,
            InputContext context)
        {
            const auto actionId = ResolveAxisAction(axis, context);
            if (!actionId) {
                return;
            }

            const auto* descriptor = backend::FindNativeActionDescriptor(*actionId);
            if (descriptor == nullptr ||
                descriptor->backend != backend::PlannedBackend::NativeState) {
                return;
            }

            switch (descriptor->axisTarget) {
            case backend::NativeAxisTarget::MoveStick:
                if (axis == PadAxisId::LeftStickX) {
                    analog.moveX = value;
                    analog.hasAnalog = true;
                } else if (axis == PadAxisId::LeftStickY) {
                    analog.moveY = value;
                    analog.hasAnalog = true;
                }
                break;

            case backend::NativeAxisTarget::LookStick:
                if (axis == PadAxisId::RightStickX) {
                    analog.lookX = value;
                    analog.hasAnalog = true;
                } else if (axis == PadAxisId::RightStickY) {
                    analog.lookY = value;
                    analog.hasAnalog = true;
                }
                break;

            case backend::NativeAxisTarget::LeftTrigger:
                if (axis == PadAxisId::LeftTrigger) {
                    analog.leftTrigger = value;
                    analog.hasAnalog = true;
                }
                break;

            case backend::NativeAxisTarget::RightTrigger:
                if (axis == PadAxisId::RightTrigger) {
                    analog.rightTrigger = value;
                    analog.hasAnalog = true;
                }
                break;

            case backend::NativeAxisTarget::None:
            default:
                break;
            }
        }
    }

    ProjectedAnalogState CollectBoundAnalogState(const SyntheticPadFrame& frame, InputContext context)
    {
        ProjectedAnalogState analog{};
        ApplyAxisBinding(analog, PadAxisId::LeftStickX, frame.leftStickX.value, context);
        ApplyAxisBinding(analog, PadAxisId::LeftStickY, frame.leftStickY.value, context);
        ApplyAxisBinding(analog, PadAxisId::RightStickX, frame.rightStickX.value, context);
        ApplyAxisBinding(analog, PadAxisId::RightStickY, frame.rightStickY.value, context);
        ApplyAxisBinding(analog, PadAxisId::LeftTrigger, frame.leftTrigger.value, context);
        ApplyAxisBinding(analog, PadAxisId::RightTrigger, frame.rightTrigger.value, context);
        return analog;
    }
}
