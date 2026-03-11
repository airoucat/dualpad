#include "pch.h"
#include "input/backend/NativeStateBackend.h"

#include "input/backend/NativeControlCode.h"

namespace dualpad::input::backend
{
    namespace
    {
        bool IsPressedState(PlannedActionPhase phase)
        {
            switch (phase) {
            case PlannedActionPhase::Pulse:
            case PlannedActionPhase::Press:
            case PlannedActionPhase::Hold:
                return true;
            case PlannedActionPhase::Release:
            case PlannedActionPhase::None:
            case PlannedActionPhase::Value:
            default:
                return false;
            }
        }
    }

    void NativeStateBackend::Reset()
    {
        _state.Reset();
        _lastContext = InputContext::Gameplay;
    }

    void NativeStateBackend::BeginFrame(InputContext context)
    {
        _lastContext = context;
        _state.BeginFrame();
    }

    void NativeStateBackend::ApplyPlan(const FrameActionPlan& plan)
    {
        for (const auto& action : plan) {
            if (action.backend != PlannedBackend::NativeState) {
                continue;
            }

            switch (action.kind) {
            case PlannedActionKind::NativeButton:
                {
                    const auto code = static_cast<NativeControlCode>(action.outputCode);
                    const auto mask = ToVirtualButtonMask(code);
                    if (mask != 0) {
                    _state.ApplyButton(mask, IsPressedState(action.phase));
                    }
                }
                break;

            case PlannedActionKind::NativeAxis1D:
                switch (static_cast<NativeControlCode>(action.outputCode)) {
                case NativeControlCode::LeftTriggerAxis:
                    _state.SetAxis(VirtualGamepadAxis::LeftTrigger, action.valueX);
                    break;
                case NativeControlCode::RightTriggerAxis:
                    _state.SetAxis(VirtualGamepadAxis::RightTrigger, action.valueX);
                    break;
                default:
                    break;
                }
                break;

            case PlannedActionKind::NativeAxis2D:
                switch (static_cast<NativeControlCode>(action.outputCode)) {
                case NativeControlCode::MoveStick:
                    _state.SetStick(VirtualGamepadStick::Move, action.valueX, action.valueY);
                    break;
                case NativeControlCode::LookStick:
                case NativeControlCode::MenuStick:
                    _state.SetStick(VirtualGamepadStick::Look, action.valueX, action.valueY);
                    break;
                default:
                    break;
                }
                break;

            case PlannedActionKind::PluginAction:
            case PlannedActionKind::ModEvent:
            default:
                break;
            }
        }
    }
}
