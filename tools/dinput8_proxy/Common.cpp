#include "Common.h"

#include <combaseapi.h>

namespace dualpad::dinput8_proxy
{
    std::string GuidToString(REFGUID guid)
    {
        wchar_t buffer[64]{};
        const auto written = ::StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
        if (written <= 0) {
            return "{guid-format-error}";
        }

        char utf8[128]{};
        const auto converted = ::WideCharToMultiByte(
            CP_UTF8,
            0,
            buffer,
            -1,
            utf8,
            static_cast<int>(std::size(utf8)),
            nullptr,
            nullptr);
        if (converted <= 0) {
            return "{guid-convert-error}";
        }

        return utf8;
    }

    std::string HResultToString(HRESULT result)
    {
        return std::format("0x{:08X}", static_cast<std::uint32_t>(result));
    }

    bool IsKeyboardGuid(REFGUID guid)
    {
        return ::IsEqualGUID(guid, GUID_SysKeyboard);
    }

    bool IsInterestingKeyboardData(const DIDEVICEOBJECTDATA& data)
    {
        return data.dwOfs == kFocusScancode;
    }
}
