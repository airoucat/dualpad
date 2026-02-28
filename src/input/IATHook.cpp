#include "pch.h"
#include "input/IATHook.h"
#include "input/SyntheticPadState.h"
#include "input/PadProfile.h"

#include <Windows.h>
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        // XInput 结构定义
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

        struct XINPUT_VIBRATION
        {
            WORD wLeftMotorSpeed;
            WORD wRightMotorSpeed;
        };

        // 原始函数指针
        using XInputGetStateFunc = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        using XInputSetStateFunc = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

        XInputGetStateFunc g_originalGetState = nullptr;
        XInputSetStateFunc g_originalSetState = nullptr;

        // 我们的 hook 函数
        DWORD WINAPI HookedXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
        {
            if (!pState || dwUserIndex != 0) {
                if (g_originalGetState) {
                    return g_originalGetState(dwUserIndex, pState);
                }
                return ERROR_DEVICE_NOT_CONNECTED;
            }

            // 获取 DualSense 状态
            auto frame = SyntheticPadState::GetSingleton().ConsumeFrame();

            // 映射按键
            WORD buttons = 0;
            const auto& bits = GetPadBits(GetActivePadProfile());

            if (frame.downMask & bits.jump) buttons |= 0x1000;      // A
            if (frame.downMask & bits.activate) buttons |= 0x1000;  // A
            if (frame.downMask & bits.cancel) buttons |= 0x2000;    // B
            if (frame.downMask & bits.sneak) buttons |= 0x0040;     // R3
            if (frame.downMask & bits.sprint) buttons |= 0x0040;    // L3
            if (frame.downMask & bits.shout) buttons |= 0x0200;     // RB

            // D-Pad
            if (frame.downMask & bits.dpadUp) buttons |= 0x0001;
            if (frame.downMask & bits.dpadDown) buttons |= 0x0002;
            if (frame.downMask & bits.dpadLeft) buttons |= 0x0004;
            if (frame.downMask & bits.dpadRight) buttons |= 0x0008;

            // L1/R1
            if (frame.downMask & bits.l1) buttons |= 0x0100;
            if (frame.downMask & bits.r1) buttons |= 0x0200;

            pState->Gamepad.wButtons = buttons;

            // 摇杆和扳机
            if (frame.hasAxis) {
                pState->Gamepad.sThumbLX = static_cast<SHORT>(frame.lx * 32767.0f);
                pState->Gamepad.sThumbLY = static_cast<SHORT>(frame.ly * 32767.0f);
                pState->Gamepad.sThumbRX = static_cast<SHORT>(frame.rx * 32767.0f);
                pState->Gamepad.sThumbRY = static_cast<SHORT>(frame.ry * 32767.0f);
                pState->Gamepad.bLeftTrigger = static_cast<BYTE>(frame.l2 * 255.0f);
                pState->Gamepad.bRightTrigger = static_cast<BYTE>(frame.r2 * 255.0f);
            }
            else {
                pState->Gamepad.sThumbLX = 0;
                pState->Gamepad.sThumbLY = 0;
                pState->Gamepad.sThumbRX = 0;
                pState->Gamepad.sThumbRY = 0;
                pState->Gamepad.bLeftTrigger = 0;
                pState->Gamepad.bRightTrigger = 0;
            }

            pState->dwPacketNumber++;
            return ERROR_SUCCESS;
        }

        DWORD WINAPI HookedXInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
        {
            if (dwUserIndex == 0 && pVibration) {
                // TODO: 转换为 DualSense 震动
                logger::trace("[DualPad] Vibration: L={} R={}",
                    pVibration->wLeftMotorSpeed, pVibration->wRightMotorSpeed);
            }

            if (g_originalSetState) {
                return g_originalSetState(dwUserIndex, pVibration);
            }
            return ERROR_SUCCESS;
        }

        // IAT Hook 实现
        bool HookIATEntry(HMODULE hModule, const char* dllName, const char* funcName,
            void* newFunc, void** oldFunc)
        {
            auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(hModule);
            if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
                return false;
            }

            auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(
                reinterpret_cast<BYTE*>(hModule) + dosHeader->e_lfanew);

            if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
                return false;
            }

            auto importDirRVA = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            if (importDirRVA == 0) {
                return false;
            }

            auto* importDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                reinterpret_cast<BYTE*>(hModule) + importDirRVA);

            // 遍历导入的 DLL
            for (; importDesc->Name; importDesc++) {
                const char* importDllName = reinterpret_cast<const char*>(
                    reinterpret_cast<BYTE*>(hModule) + importDesc->Name);

                if (_stricmp(importDllName, dllName) != 0) {
                    continue;
                }

                logger::info("[DualPad][IAT] Found import DLL: {}", importDllName);

                // 遍历导入的函数
                auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                    reinterpret_cast<BYTE*>(hModule) + importDesc->FirstThunk);

                auto* origThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                    reinterpret_cast<BYTE*>(hModule) + importDesc->OriginalFirstThunk);

                for (; thunk->u1.Function; thunk++, origThunk++) {
                    bool isOrdinal = IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal);

                    if (isOrdinal) {
                        // 序号导入
                        WORD ordinal = IMAGE_ORDINAL(origThunk->u1.Ordinal);

                        bool shouldHook = false;
                        if (strcmp(funcName, "XInputGetState") == 0 && ordinal == 2) {
                            shouldHook = true;
                        }
                        else if (strcmp(funcName, "XInputSetState") == 0 && ordinal == 3) {
                            shouldHook = true;
                        }

                        if (!shouldHook) {
                            continue;
                        }

                        logger::info("[DualPad][IAT] Matched {} to ordinal #{}", funcName, ordinal);
                    }
                    else {
                        // 名称导入
                        auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                            reinterpret_cast<BYTE*>(hModule) + origThunk->u1.AddressOfData);

                        if (strcmp(importByName->Name, funcName) != 0) {
                            continue;
                        }

                        logger::info("[DualPad][IAT] Found function: {}", funcName);
                    }

                    // 保存原函数地址
                    if (oldFunc) {
                        *oldFunc = reinterpret_cast<void*>(thunk->u1.Function);
                    }

                    // 修改为我们的函数
                    DWORD oldProtect;
                    if (!VirtualProtect(&thunk->u1.Function, sizeof(ULONGLONG),
                        PAGE_READWRITE, &oldProtect)) {
                        logger::error("[DualPad][IAT] VirtualProtect failed");
                        return false;
                    }

                    thunk->u1.Function = reinterpret_cast<ULONGLONG>(newFunc);

                    VirtualProtect(&thunk->u1.Function, sizeof(ULONGLONG), oldProtect, &oldProtect);

                    logger::info("[DualPad][IAT] Hooked {}", funcName);
                    return true;
                }

                return false;
            }

            return false;
        }
    }

    bool InstallXInputIATHook()
    {
        logger::info("[DualPad][IAT] Installing XInput IAT hooks");

        HMODULE hSkyrim = GetModuleHandleA(nullptr);
        if (!hSkyrim) {
            logger::error("[DualPad][IAT] Failed to get Skyrim module handle");
            return false;
        }

        bool foundAny = false;

        // Hook XInputGetState
        if (HookIATEntry(hSkyrim, "xinput1_3.dll", "XInputGetState",
            reinterpret_cast<void*>(HookedXInputGetState),
            reinterpret_cast<void**>(&g_originalGetState))) {
            logger::info("[DualPad][IAT] XInputGetState hooked");
            foundAny = true;
        }

        // Hook XInputSetState
        if (HookIATEntry(hSkyrim, "xinput1_3.dll", "XInputSetState",
            reinterpret_cast<void*>(HookedXInputSetState),
            reinterpret_cast<void**>(&g_originalSetState))) {
            logger::info("[DualPad][IAT] XInputSetState hooked");
            foundAny = true;
        }

        if (!foundAny) {
            logger::warn("[DualPad][IAT] Skyrim does not import XInput functions");
        }

        return foundAny;
    }
}