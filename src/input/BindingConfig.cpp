#include "pch.h"
#include "input/BindingConfig.h"
#include "input/BindingManager.h"
#include "input/InputContext.h"
#include "input/Trigger.h"

#include <SKSE/SKSE.h>
#include <fstream>
#include <sstream>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        inline std::string Trim(std::string s)
        {
            auto isSpace = [](unsigned char c) {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
                };

            while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) {
                s.erase(s.begin());
            }
            while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) {
                s.pop_back();
            }
            return s;
        }

        InputContext StringToContext(std::string_view str)
        {
            if (str == "Gameplay") return InputContext::Gameplay;
            if (str == "Menu") return InputContext::Menu;
            if (str == "InventoryMenu") return InputContext::InventoryMenu;
            if (str == "MagicMenu") return InputContext::MagicMenu;
            if (str == "MapMenu") return InputContext::MapMenu;
            if (str == "JournalMenu") return InputContext::JournalMenu;
            if (str == "DialogueMenu") return InputContext::DialogueMenu;
            if (str == "FavoritesMenu") return InputContext::FavoritesMenu;
            if (str == "TweenMenu") return InputContext::TweenMenu;
            if (str == "ContainerMenu") return InputContext::ContainerMenu;
            if (str == "BarterMenu") return InputContext::BarterMenu;
            if (str == "Lockpicking") return InputContext::Lockpicking;
            if (str == "Combat") return InputContext::Combat;
            if (str == "Sneaking") return InputContext::Sneaking;
            if (str == "Riding") return InputContext::Riding;
            if (str == "Console") return InputContext::Console;
            if (str == "Book") return InputContext::Book;
            if (str == "CreationsMenu") return InputContext::CreationsMenu;

            return InputContext::Unknown;
        }

        std::uint32_t GestureNameToCode(std::string_view name)
        {
            if (name == "TpLeftPress") return 0x01000000;
            if (name == "TpMidPress") return 0x02000000;
            if (name == "TpRightPress") return 0x04000000;
            if (name == "TpSwipeUp") return 0x08000000;
            if (name == "TpSwipeDown") return 0x10000000;
            if (name == "TpSwipeLeft") return 0x20000000;
            if (name == "TpSwipeRight") return 0x40000000;
            return 0;
        }

        std::uint32_t ButtonNameToCode(std::string_view name)
        {
            if (name == "Square") return 0x00000001;
            if (name == "Cross") return 0x00000002;
            if (name == "Circle") return 0x00000004;
            if (name == "Triangle") return 0x00000008;
            if (name == "L1") return 0x00000010;
            if (name == "R1") return 0x00000020;
            if (name == "L2Button") return 0x00000040;
            if (name == "R2Button") return 0x00000080;
            if (name == "Create") return 0x00000100;
            if (name == "Options") return 0x00000200;
            if (name == "L3") return 0x00000400;
            if (name == "R3") return 0x00000800;
            if (name == "PS") return 0x00001000;
            if (name == "Mic") return 0x00002000;
            if (name == "DpadUp") return 0x00010000;
            if (name == "DpadDown") return 0x00020000;
            if (name == "DpadLeft") return 0x00040000;
            if (name == "DpadRight") return 0x00080000;
            if (name == "FnLeft") return 0x00100000;
            if (name == "FnRight") return 0x00200000;
            if (name == "BackLeft") return 0x00400000;
            if (name == "BackRight") return 0x00800000;
            return 0;
        }
    }

    BindingConfig& BindingConfig::GetSingleton()
    {
        static BindingConfig instance;
        return instance;
    }

    std::filesystem::path BindingConfig::GetDefaultPath()
    {
        return "Data/SKSE/Plugins/DualPadBindings.ini";
    }

    bool BindingConfig::Load(const std::filesystem::path& path)
    {
        _configPath = path.empty() ? GetDefaultPath() : path;

        logger::info("[DualPad][Config] Loading bindings from: {}", _configPath.string());

        if (!std::filesystem::exists(_configPath)) {
            logger::warn("[DualPad][Config] Config file not found, using defaults");
            BindingManager::GetSingleton().InitDefaultBindings();
            return false;
        }

        return ParseIniFile(_configPath);
    }

    bool BindingConfig::Reload()
    {
        if (_configPath.empty()) {
            return Load();
        }
        return ParseIniFile(_configPath);
    }

    bool BindingConfig::ParseTrigger(std::string_view triggerStr, Trigger& outTrigger)
    {
        // 格式: "Gesture:TpLeftPress" 或 "Button:BackLeft" 或 "Button:FnLeft+Triangle"

        auto colonPos = triggerStr.find(':');
        if (colonPos == std::string_view::npos) {
            return false;
        }

        auto typeStr = triggerStr.substr(0, colonPos);
        auto codeStr = triggerStr.substr(colonPos + 1);

        // 解析类型
        if (typeStr == "Gesture") {
            outTrigger.type = TriggerType::Gesture;
            outTrigger.code = GestureNameToCode(codeStr);
            return outTrigger.code != 0;
        }

        if (typeStr == "Button") {
            outTrigger.type = TriggerType::Button;

            // 检查是否有组合键
            auto plusPos = codeStr.find('+');
            if (plusPos != std::string_view::npos) {
                // 组合键: "FnLeft+Triangle"
                auto modifierStr = codeStr.substr(0, plusPos);
                auto mainStr = codeStr.substr(plusPos + 1);

                auto modifierCode = ButtonNameToCode(modifierStr);
                auto mainCode = ButtonNameToCode(mainStr);

                if (modifierCode == 0 || mainCode == 0) {
                    return false;
                }

                outTrigger.code = mainCode;
                outTrigger.modifiers.push_back(modifierCode);
                return true;
            }

            // 单个按键
            outTrigger.code = ButtonNameToCode(codeStr);
            return outTrigger.code != 0;
        }

        return false;
    }

    bool BindingConfig::ParseBinding(std::string_view contextStr, std::string_view key, std::string_view value)
    {
        // 特殊键: Inherit
        if (key == "Inherit") {
            // TODO: 实现继承逻辑
            logger::info("[DualPad][Config] Context {} inherits from {}", contextStr, value);
            return true;
        }

        // 解析上下文
        auto context = StringToContext(contextStr);
        if (context == InputContext::Unknown) {
            logger::warn("[DualPad][Config] Unknown context: {}", contextStr);
            return false;
        }

        // 解析触发器
        Trigger trigger;
        if (!ParseTrigger(key, trigger)) {
            logger::warn("[DualPad][Config] Invalid trigger: {}", key);
            return false;
        }

        // 创建绑定
        Binding binding;
        binding.trigger = trigger;
        binding.actionId = std::string(value);
        binding.context = context;

        BindingManager::GetSingleton().AddBinding(binding);

        logger::trace("[DualPad][Config] Bound {} -> {} in {}",
            key, value, contextStr);

        return true;
    }

    bool BindingConfig::ParseIniFile(const std::filesystem::path& path)
    {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            logger::error("[DualPad][Config] Failed to open: {}", path.string());
            return false;
        }

        std::string currentSection;
        std::string line;
        std::size_t lineNo = 0;
        std::size_t bindingCount = 0;

        while (std::getline(ifs, line)) {
            ++lineNo;

            // 去除 BOM
            if (lineNo == 1 && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }

            line = Trim(line);

            // 跳过空行和注释
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            // 解析节
            if (line.front() == '[' && line.back() == ']') {
                currentSection = Trim(line.substr(1, line.size() - 2));
                logger::info("[DualPad][Config] Parsing section: [{}]", currentSection);
                continue;
            }

            // 解析键值对
            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                logger::warn("[DualPad][Config] Line {}: Invalid format (no '=')", lineNo);
                continue;
            }

            auto key = Trim(line.substr(0, eqPos));
            auto value = Trim(line.substr(eqPos + 1));

            if (key.empty() || value.empty()) {
                continue;
            }

            if (currentSection.empty()) {
                logger::warn("[DualPad][Config] Line {}: No section defined", lineNo);
                continue;
            }

            if (ParseBinding(currentSection, key, value)) {
                ++bindingCount;
            }
        }

        logger::info("[DualPad][Config] Loaded {} bindings from {}", bindingCount, path.string());
        return bindingCount > 0;
    }
}