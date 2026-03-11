#pragma once

#include "Common.h"

namespace dualpad::dinput8_proxy
{
    using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

    HRESULT CallRealDirectInput8Create(
        HINSTANCE hinst,
        DWORD version,
        REFIID riid,
        LPVOID* output,
        LPUNKNOWN outer);
}
