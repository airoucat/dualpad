#include "pch.h"
#include "input/XInputStateBridge.h"

#include "input/PadProfile.h"
#include "input/SyntheticPadState.h"
#include "input/backend/ButtonEventBackend.h"

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
        // UpstreamGamepadHook drains pending snapshots immediately before this
        // bridge runs, so both the compatibility cache and ButtonEvent poll
        // commit represent the latest-known state for this Poll window rather
        // than an arbitrary "previous frame" replay.
        auto frame = SyntheticPadState::GetSingleton().ConsumeFrame();
        const auto committedButtons = backend::ButtonEventBackend::GetSingleton().CommitPollState();

        const auto legacyDownMask = frame.downMask & ~committedButtons.managedMask;
        const auto combinedDownMask = legacyDownMask | committedButtons.semanticDownMask;

        WORD buttons = 0;
        const auto& bits = GetPadBits(GetActivePadProfile());

        if (combinedDownMask & bits.cross) buttons |= 0x1000;
        if (combinedDownMask & bits.circle) buttons |= 0x2000;
        if (combinedDownMask & bits.square) buttons |= 0x4000;
        if (combinedDownMask & bits.triangle) buttons |= 0x8000;

        if (combinedDownMask & bits.l1) buttons |= 0x0100;
        if (combinedDownMask & bits.r1) buttons |= 0x0200;

        if (combinedDownMask & bits.l3) buttons |= 0x0040;
        if (combinedDownMask & bits.r3) buttons |= 0x0080;

        if (combinedDownMask & bits.options) buttons |= 0x0010;
        if (combinedDownMask & bits.create) buttons |= 0x0020;

        if (combinedDownMask & bits.dpadUp) buttons |= 0x0001;
        if (combinedDownMask & bits.dpadDown) buttons |= 0x0002;
        if (combinedDownMask & bits.dpadLeft) buttons |= 0x0004;
        if (combinedDownMask & bits.dpadRight) buttons |= 0x0008;

        state->Gamepad.wButtons = buttons;

        if (frame.hasAxis) {
            state->Gamepad.sThumbLX = static_cast<SHORT>(frame.lx * 32767.0f);
            state->Gamepad.sThumbLY = static_cast<SHORT>(frame.ly * 32767.0f);
            state->Gamepad.sThumbRX = static_cast<SHORT>(frame.rx * 32767.0f);
            state->Gamepad.sThumbRY = static_cast<SHORT>(frame.ry * 32767.0f);
            state->Gamepad.bLeftTrigger = static_cast<BYTE>(frame.l2 * 255.0f);
            state->Gamepad.bRightTrigger = static_cast<BYTE>(frame.r2 * 255.0f);
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
