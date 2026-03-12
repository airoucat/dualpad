#include "pch.h"
#include "input/backend/NativeStateBackend.h"

#include "input/backend/NativeButtonMapping.h"
#include "input/backend/NativeControlCode.h"
#include "input/backend/PollInputAllowance.h"

namespace dualpad::input::backend
{
    void NativeStateBackend::Reset()
    {
        _digitalCoordinator.Reset();
        _state.Reset();
        _lastCommitFrame = {};
        _lastContext = InputContext::Gameplay;
        _pollIndex = 0;
        _packetNumber = 0;
    }

    void NativeStateBackend::BeginFrame(InputContext context)
    {
        _lastContext = context;
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
                    if (IsDigitalNativeControl(code)) {
                        _digitalCoordinator.NoteButtonAction(
                            code,
                            action.phase,
                            action.timestampUs,
                            action.lifecycle);
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

    const VirtualGamepadState& NativeStateBackend::CommitPollState(std::uint64_t pollTimestampUs)
    {
        // Sample all commit-allowance families once per Poll so lifecycle
        // policy can defer by gate class without falling back to per-action
        // if-else fixes in the injection layer.
        const auto allowance = SamplePollInputAllowance(_lastContext);
        _digitalCoordinator.SetPollAllowance(allowance);
        _lastCommitFrame = _digitalCoordinator.Commit(pollTimestampUs);

        std::uint16_t rawButtons = 0;
        for (const auto& slot : _digitalCoordinator.GetSlots()) {
            if (slot.committedDown) {
                rawButtons |= ResolveMappedGamepadButton(slot.control);
            }
        }

        ++_pollIndex;
        ++_packetNumber;
        _state.SetCommittedDigitalState(
            _pollIndex,
            _packetNumber,
            _lastCommitFrame.nextDownMask,
            rawButtons);
        return _state;
    }

    void NativeStateBackend::SetRawAnalogState(
        float moveX,
        float moveY,
        float lookX,
        float lookY,
        float leftTrigger,
        float rightTrigger)
    {
        _state.SetStick(VirtualGamepadStick::Move, moveX, moveY);
        _state.SetStick(VirtualGamepadStick::Look, lookX, lookY);
        _state.SetAxis(VirtualGamepadAxis::LeftTrigger, leftTrigger);
        _state.SetAxis(VirtualGamepadAxis::RightTrigger, rightTrigger);
    }
}
