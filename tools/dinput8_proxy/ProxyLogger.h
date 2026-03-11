#pragma once

#include "Common.h"

#include <format>
#include <mutex>
#include <string_view>

namespace dualpad::dinput8_proxy
{
    void InitializeProxyLogger(HMODULE module);
    void LogLine(std::string_view line);

    template <class... Args>
    void Logf(std::format_string<Args...> format, Args&&... args)
    {
        LogLine(std::format(format, std::forward<Args>(args)...));
    }
}
