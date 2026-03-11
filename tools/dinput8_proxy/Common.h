#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#include <Windows.h>
#include <dinput.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace dualpad::dinput8_proxy
{
    inline constexpr DWORD kFocusScancode = 0x39;

    std::string GuidToString(REFGUID guid);
    std::string HResultToString(HRESULT result);
    bool IsKeyboardGuid(REFGUID guid);
    bool IsInterestingKeyboardData(const DIDEVICEOBJECTDATA& data);
}
