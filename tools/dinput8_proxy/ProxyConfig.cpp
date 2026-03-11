#include "ProxyConfig.h"

#include "ProxyLogger.h"

#include <cstdlib>
#include <cstring>
#include <charconv>
#include <filesystem>
#include <mutex>

namespace dualpad::dinput8_proxy
{
    extern "C" IMAGE_DOS_HEADER __ImageBase;

    namespace
    {
        std::once_flag g_configOnce;
        ProxyConfig g_config{};

        std::filesystem::path GetConfigPath()
        {
            wchar_t modulePath[MAX_PATH]{};
            const auto written = ::GetModuleFileNameW(
                reinterpret_cast<HMODULE>(&__ImageBase),
                modulePath,
                static_cast<DWORD>(std::size(modulePath)));
            if (written == 0) {
                return std::filesystem::current_path() / "DualPadDInput8.ini";
            }

            return std::filesystem::path(modulePath).parent_path() / "DualPadDInput8.ini";
        }

        bool ReadBool(const std::filesystem::path& path, const char* section, const char* key, bool defaultValue)
        {
            char buffer[32]{};
            ::GetPrivateProfileStringA(
                section,
                key,
                defaultValue ? "1" : "0",
                buffer,
                static_cast<DWORD>(std::size(buffer)),
                path.string().c_str());
            return buffer[0] != '0';
        }

        DWORD ReadDword(const std::filesystem::path& path, const char* section, const char* key, DWORD defaultValue)
        {
            char buffer[64]{};
            const auto defaultString = std::format("0x{:X}", defaultValue);
            ::GetPrivateProfileStringA(
                section,
                key,
                defaultString.c_str(),
                buffer,
                static_cast<DWORD>(std::size(buffer)),
                path.string().c_str());

            char* end = nullptr;
            const auto parsed = std::strtoul(buffer, &end, 0);
            if (end != nullptr && *end == '\0') {
                return static_cast<DWORD>(parsed);
            }

            return defaultValue;
        }

        void LoadConfig()
        {
            const auto path = GetConfigPath();
            g_config.logOnlyInteresting = ReadBool(path, "logging", "log_only_interesting", true);

            Logf(
                "Loaded config path={} logOnlyInteresting={}",
                path.string(),
                g_config.logOnlyInteresting);
        }
    }

    const ProxyConfig& GetProxyConfig()
    {
        std::call_once(g_configOnce, LoadConfig);
        return g_config;
    }
}
