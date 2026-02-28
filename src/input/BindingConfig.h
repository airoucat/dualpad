#pragma once
#include <string>
#include <filesystem>

namespace dualpad::input
{
    // 前向声明
    struct Trigger;

    class BindingConfig
    {
    public:
        static BindingConfig& GetSingleton();

        // 加载配置文件
        bool Load(const std::filesystem::path& path = {});

        // 重新加载（热重载）
        bool Reload();

        // 获取默认配置路径
        static std::filesystem::path GetDefaultPath();

    private:
        BindingConfig() = default;

        std::filesystem::path _configPath;

        bool ParseIniFile(const std::filesystem::path& path);
        bool ParseBinding(std::string_view context, std::string_view key, std::string_view value);
        bool ParseTrigger(std::string_view triggerStr, Trigger& outTrigger);
    };
}