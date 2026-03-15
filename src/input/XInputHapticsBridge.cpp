#include "pch.h"
#include "input/XInputHapticsBridge.h"
#include "haptics/HapticsSystem.h"

#include <Windows.h>
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        struct XINPUT_VIBRATION
        {
            WORD wLeftMotorSpeed;
            WORD wRightMotorSpeed;
        };

        using XInputSetStateFunc = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

        XInputSetStateFunc g_originalSetState = nullptr;

        // Routes game vibration requests into the native DualSense haptics path.
        DWORD WINAPI HookedXInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
        {
            if (dwUserIndex == 0) {
                auto& hapticsSystem = dualpad::haptics::HapticsSystem::GetSingleton();

                if (pVibration) {
                    (void)hapticsSystem.SubmitNativeVibration(
                        pVibration->wLeftMotorSpeed,
                        pVibration->wRightMotorSpeed);
                }
                else {
                    (void)hapticsSystem.SubmitNativeVibration(0, 0);
                }

                return ERROR_SUCCESS;
            }

            if (g_originalSetState) {
                return g_originalSetState(dwUserIndex, pVibration);
            }
            return ERROR_SUCCESS;
        }

        // Rewrites one import address table slot in the Skyrim module.
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

            for (; importDesc->Name; importDesc++) {
                const char* importDllName = reinterpret_cast<const char*>(
                    reinterpret_cast<BYTE*>(hModule) + importDesc->Name);

                if (_stricmp(importDllName, dllName) != 0) {
                    continue;
                }

                logger::info("[DualPad][XInputHaptics] Found import DLL: {}", importDllName);

                auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                    reinterpret_cast<BYTE*>(hModule) + importDesc->FirstThunk);

                auto* origThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                    reinterpret_cast<BYTE*>(hModule) + importDesc->OriginalFirstThunk);

                for (; thunk->u1.Function; thunk++, origThunk++) {
                    bool isOrdinal = IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal);

                    if (isOrdinal) {
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

                        logger::info("[DualPad][XInputHaptics] Matched {} to ordinal #{}", funcName, ordinal);
                    }
                    else {
                        auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                            reinterpret_cast<BYTE*>(hModule) + origThunk->u1.AddressOfData);

                        if (strcmp(importByName->Name, funcName) != 0) {
                            continue;
                        }

                        logger::info("[DualPad][XInputHaptics] Found function: {}", funcName);
                    }

                    if (oldFunc) {
                        *oldFunc = reinterpret_cast<void*>(thunk->u1.Function);
                    }

                    DWORD oldProtect;
                    if (!VirtualProtect(&thunk->u1.Function, sizeof(ULONGLONG),
                        PAGE_READWRITE, &oldProtect)) {
                        logger::error("[DualPad][XInputHaptics] VirtualProtect failed");
                        return false;
                    }

                    thunk->u1.Function = reinterpret_cast<ULONGLONG>(newFunc);

                    VirtualProtect(&thunk->u1.Function, sizeof(ULONGLONG), oldProtect, &oldProtect);

                    logger::info("[DualPad][XInputHaptics] Hooked {}", funcName);
                    return true;
                }

                return false;
            }

            return false;
        }
    }

    bool InstallXInputHapticsBridge()
    {
        logger::info("[DualPad][XInputHaptics] Installing XInputSetState bridge");

        HMODULE hSkyrim = GetModuleHandleA(nullptr);
        if (!hSkyrim) {
            logger::error("[DualPad][XInputHaptics] Failed to get Skyrim module handle");
            return false;
        }

        if (HookIATEntry(hSkyrim, "xinput1_3.dll", "XInputSetState",
            reinterpret_cast<void*>(HookedXInputSetState),
            reinterpret_cast<void**>(&g_originalSetState))) {
            logger::info("[DualPad][XInputHaptics] XInputSetState hooked");
            return true;
        }

        logger::warn("[DualPad][XInputHaptics] Skyrim does not import XInputSetState");
        return false;
    }
}
