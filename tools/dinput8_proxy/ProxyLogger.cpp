#include "ProxyLogger.h"

#include <filesystem>
#include <fstream>

namespace dualpad::dinput8_proxy
{
    namespace
    {
        std::mutex g_logMutex;
        std::filesystem::path g_logPath;
        bool g_initialized = false;

        std::string MakeTimestamp()
        {
            SYSTEMTIME now{};
            ::GetLocalTime(&now);
            return std::format(
                "[{:02}:{:02}:{:02}.{:03}]",
                now.wHour,
                now.wMinute,
                now.wSecond,
                now.wMilliseconds);
        }
    }

    void InitializeProxyLogger(HMODULE module)
    {
        const std::scoped_lock lock(g_logMutex);
        if (g_initialized) {
            return;
        }

        wchar_t modulePath[MAX_PATH]{};
        const auto written = ::GetModuleFileNameW(module, modulePath, static_cast<DWORD>(std::size(modulePath)));
        if (written > 0) {
            g_logPath = std::filesystem::path(modulePath).parent_path() / "DualPadDInput8.log";
        } else {
            g_logPath = std::filesystem::current_path() / "DualPadDInput8.log";
        }

        g_initialized = true;
        std::ofstream out(g_logPath, std::ios::trunc);
        if (out.is_open()) {
            out << MakeTimestamp() << " [DualPadDInput8] logger initialized (log reset)" << '\n';
        }
    }

    void LogLine(std::string_view line)
    {
        const std::scoped_lock lock(g_logMutex);
        if (!g_initialized || g_logPath.empty()) {
            return;
        }

        std::ofstream out(g_logPath, std::ios::app);
        if (!out.is_open()) {
            return;
        }

        out << MakeTimestamp() << " [DualPadDInput8] " << line << '\n';
        out.flush();
    }
}
