#include "pch.h"
#include "input/XInputStateBridge.h"

#include "input/AuthoritativePollState.h"
#include "input/XInputButtonSerialization.h"
#include "input/backend/FrameActionPlanDebugLogger.h"

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

        std::atomic<DWORD> g_syntheticPacketNumber{ 0 };

        struct SyntheticXInputSnapshot
        {
            WORD buttons{ 0 };
            BYTE leftTrigger{ 0 };
            BYTE rightTrigger{ 0 };
            SHORT thumbLX{ 0 };
            SHORT thumbLY{ 0 };
            SHORT thumbRX{ 0 };
            SHORT thumbRY{ 0 };
        };

        SyntheticXInputSnapshot g_lastSnapshot{};
        bool g_hasLastSnapshot{ false };

        bool SameSnapshot(
            const SyntheticXInputSnapshot& lhs,
            const SyntheticXInputSnapshot& rhs) noexcept
        {
            return lhs.buttons == rhs.buttons &&
                lhs.leftTrigger == rhs.leftTrigger &&
                lhs.rightTrigger == rhs.rightTrigger &&
                lhs.thumbLX == rhs.thumbLX &&
                lhs.thumbLY == rhs.thumbLY &&
                lhs.thumbRX == rhs.thumbRX &&
                lhs.thumbRY == rhs.thumbRY;
        }
    }

    std::uint32_t FillSyntheticXInputState(void* pState)
    {
        if (!pState) {
            return ERROR_BAD_ARGUMENTS;
        }

        auto* state = reinterpret_cast<XINPUT_STATE*>(pState);
        const auto frame = AuthoritativePollState::GetSingleton().ReadSnapshot();
        backend::LogAuthoritativePollFrame(frame);
        state->Gamepad.wButtons = ToXInputButtons(frame.downMask);

        if (frame.hasAnalog) {
            state->Gamepad.sThumbLX = static_cast<SHORT>(frame.moveX * 32767.0f);
            state->Gamepad.sThumbLY = static_cast<SHORT>(frame.moveY * 32767.0f);
            state->Gamepad.sThumbRX = static_cast<SHORT>(frame.lookX * 32767.0f);
            state->Gamepad.sThumbRY = static_cast<SHORT>(frame.lookY * 32767.0f);
            state->Gamepad.bLeftTrigger = static_cast<BYTE>(frame.leftTrigger * 255.0f);
            state->Gamepad.bRightTrigger = static_cast<BYTE>(frame.rightTrigger * 255.0f);
        }
        else {
            state->Gamepad.sThumbLX = 0;
            state->Gamepad.sThumbLY = 0;
            state->Gamepad.sThumbRX = 0;
            state->Gamepad.sThumbRY = 0;
            state->Gamepad.bLeftTrigger = 0;
            state->Gamepad.bRightTrigger = 0;
        }

        const SyntheticXInputSnapshot currentSnapshot{
            .buttons = state->Gamepad.wButtons,
            .leftTrigger = state->Gamepad.bLeftTrigger,
            .rightTrigger = state->Gamepad.bRightTrigger,
            .thumbLX = state->Gamepad.sThumbLX,
            .thumbLY = state->Gamepad.sThumbLY,
            .thumbRX = state->Gamepad.sThumbRX,
            .thumbRY = state->Gamepad.sThumbRY
        };

        if (!g_hasLastSnapshot || !SameSnapshot(currentSnapshot, g_lastSnapshot)) {
            g_lastSnapshot = currentSnapshot;
            g_hasLastSnapshot = true;
            state->dwPacketNumber =
                g_syntheticPacketNumber.fetch_add(1, std::memory_order_relaxed) + 1;
        } else {
            state->dwPacketNumber = g_syntheticPacketNumber.load(std::memory_order_relaxed);
        }

        return ERROR_SUCCESS;
    }
}
