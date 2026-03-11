#include "SystemDInput8Loader.h"

#include "ProxyLogger.h"

#include <filesystem>
#include <mutex>

namespace dualpad::dinput8_proxy
{
    namespace
    {
        std::once_flag g_loaderOnce;
        HMODULE g_systemModule = nullptr;
        DirectInput8Create_t g_directInput8Create = nullptr;
        HRESULT g_loaderStatus = E_FAIL;

        void InitializeLoader()
        {
            wchar_t systemDirectory[MAX_PATH]{};
            const auto written =
                ::GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
            if (written == 0 || written >= std::size(systemDirectory)) {
                Logf("GetSystemDirectoryW failed gle=0x{:08X}", ::GetLastError());
                g_loaderStatus = HRESULT_FROM_WIN32(::GetLastError());
                return;
            }

            const auto systemPath = std::filesystem::path(systemDirectory) / "dinput8.dll";
            g_systemModule = ::LoadLibraryW(systemPath.c_str());
            if (g_systemModule == nullptr) {
                Logf(
                    "LoadLibraryW(system dinput8) failed path={} gle=0x{:08X}",
                    systemPath.string(),
                    ::GetLastError());
                g_loaderStatus = HRESULT_FROM_WIN32(::GetLastError());
                return;
            }

            g_directInput8Create = reinterpret_cast<DirectInput8Create_t>(
                ::GetProcAddress(g_systemModule, "DirectInput8Create"));
            if (g_directInput8Create == nullptr) {
                Logf("GetProcAddress(DirectInput8Create) failed gle=0x{:08X}", ::GetLastError());
                g_loaderStatus = HRESULT_FROM_WIN32(::GetLastError());
                return;
            }

            Logf(
                "Loaded system dinput8 path={} module=0x{:X}",
                systemPath.string(),
                reinterpret_cast<std::uintptr_t>(g_systemModule));
            g_loaderStatus = S_OK;
        }
    }

    HRESULT CallRealDirectInput8Create(
        HINSTANCE hinst,
        DWORD version,
        REFIID riid,
        LPVOID* output,
        LPUNKNOWN outer)
    {
        std::call_once(g_loaderOnce, InitializeLoader);
        if (FAILED(g_loaderStatus) || g_directInput8Create == nullptr) {
            return FAILED(g_loaderStatus) ? g_loaderStatus : E_FAIL;
        }

        return g_directInput8Create(hinst, version, riid, output, outer);
    }
}
