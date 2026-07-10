#include "pch.h"
#include "input/XInputStateBridge.h"

#include <Windows.h>

namespace dualpad::input
{
    namespace
    {
        struct XINPUT_GAMEPAD
        {
            WORD  wButtons;
            BYTE  bLeftTrigger;
            BYTE  bRightTrigger;
            SHORT sThumbLX;
            SHORT sThumbLY;
            SHORT sThumbRX;
            SHORT sThumbRY;
        };

        struct XINPUT_STATE
        {
            DWORD dwPacketNumber;
            XINPUT_GAMEPAD Gamepad;
        };

    }

    std::uint32_t FillSyntheticXInputState(
        void* pState,
        const input_v2::gameplay::PollOutputFrame& frame)
    {
        if (!pState) {
            return ERROR_BAD_ARGUMENTS;
        }

        auto* state = reinterpret_cast<XINPUT_STATE*>(pState);
        state->Gamepad.wButtons = frame.buttons;
        state->Gamepad.sThumbLX = frame.lx;
        state->Gamepad.sThumbLY = frame.ly;
        state->Gamepad.sThumbRX = frame.rx;
        state->Gamepad.sThumbRY = frame.ry;
        state->Gamepad.bLeftTrigger = frame.lt;
        state->Gamepad.bRightTrigger = frame.rt;
        state->dwPacketNumber = frame.packetNumber;
        return ERROR_SUCCESS;
    }
}
