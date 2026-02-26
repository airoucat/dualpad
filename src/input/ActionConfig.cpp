#include "pch.h"
#include "input/ActionConfig.h"
#include "input/ActionRouter.h"
#include "input/InputActions.h"
#include "input/GameActions.h"

#include <SKSE/SKSE.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        struct HotReloadState
        {
            std::filesystem::path path;
            bool initialized{ false };
            bool fileSeenOnce{ false };
            std::filesystem::file_time_type lastWrite{};
            std::chrono::steady_clock::time_point nextPoll{};
        };

        HotReloadState g_cfg;

        constexpr auto kPollInterval = std::chrono::milliseconds(1000);

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

        inline bool StartsWith(std::string_view s, std::string_view p)
        {
            return s.size() >= p.size() && s.substr(0, p.size()) == p;
        }

        bool TryParseTriggerPhase(std::string_view s, TriggerPhase& out)
        {
            if (s == "Press") { out = TriggerPhase::Press; return true; }
            if (s == "Release") { out = TriggerPhase::Release; return true; }
            if (s == "Pulse") { out = TriggerPhase::Pulse; return true; }
            return false;
        }

        bool TryParseTriggerCode(std::string_view s, TriggerCode& out)
        {
            using P = std::pair<std::string_view, TriggerCode>;
            static constexpr P kMap[] = {
                { "Square", TriggerCode::Square },
                { "Cross", TriggerCode::Cross },
                { "Circle", TriggerCode::Circle },
                { "Triangle", TriggerCode::Triangle },

                { "L1", TriggerCode::L1 },
                { "R1", TriggerCode::R1 },
                { "L2_Button", TriggerCode::L2Button },
                { "R2_Button", TriggerCode::R2Button },
                { "Create", TriggerCode::Create },
                { "Options", TriggerCode::Options },
                { "L3", TriggerCode::L3 },
                { "R3", TriggerCode::R3 },
                { "PS", TriggerCode::PS },
                { "Mic", TriggerCode::Mic },
                { "TouchpadClick", TriggerCode::TouchpadClick },

                { "DPadUp", TriggerCode::DpadUp },
                { "DPadUpRight", TriggerCode::DpadUpRight },
                { "DPadRight", TriggerCode::DpadRight },
                { "DPadDownRight", TriggerCode::DpadDownRight },
                { "DPadDown", TriggerCode::DpadDown },
                { "DPadDownLeft", TriggerCode::DpadDownLeft },
                { "DPadLeft", TriggerCode::DpadLeft },
                { "DPadUpLeft", TriggerCode::DpadUpLeft },

                { "FnLeft", TriggerCode::FnLeft },
                { "FnRight", TriggerCode::FnRight },
                { "BackLeft", TriggerCode::BackLeft },
                { "BackRight", TriggerCode::BackRight },

                { "TP_LEFT_PRESS", TriggerCode::TpLeftPress },
                { "TP_MID_PRESS", TriggerCode::TpMidPress },
                { "TP_RIGHT_PRESS", TriggerCode::TpRightPress },

                { "TP_SWIPE_UP", TriggerCode::TpSwipeUp },
                { "TP_SWIPE_DOWN", TriggerCode::TpSwipeDown },
                { "TP_SWIPE_LEFT", TriggerCode::TpSwipeLeft },
                { "TP_SWIPE_RIGHT", TriggerCode::TpSwipeRight },

                { "None", TriggerCode::None },

                // 兼容别名
                { "TpLeftPress", TriggerCode::TpLeftPress },
                { "TpMidPress", TriggerCode::TpMidPress },
                { "TpRightPress", TriggerCode::TpRightPress },
                { "TpSwipeUp", TriggerCode::TpSwipeUp },
                { "TpSwipeDown", TriggerCode::TpSwipeDown },
                { "TpSwipeLeft", TriggerCode::TpSwipeLeft },
                { "TpSwipeRight", TriggerCode::TpSwipeRight },

                { "LStickUp", TriggerCode::LStickUp },
                { "LStickDown", TriggerCode::LStickDown },
                { "LStickLeft", TriggerCode::LStickLeft },
                { "LStickRight", TriggerCode::LStickRight },
                { "RStickUp", TriggerCode::RStickUp },
                { "RStickDown", TriggerCode::RStickDown },
                { "RStickLeft", TriggerCode::RStickLeft },
                { "RStickRight", TriggerCode::RStickRight },
            };

            for (auto& [name, code] : kMap) {
                if (s == name) {
                    out = code;
                    return true;
                }
            }

            // 兼容更多别名（如果需要）
            if (s == "TpMidPress") { out = TriggerCode::TpMidPress; return true; }
            if (s == "TpSwipeUp") { out = TriggerCode::TpSwipeUp; return true; }
            if (s == "TpSwipeDown") { out = TriggerCode::TpSwipeDown; return true; }
            if (s == "TpSwipeLeft") { out = TriggerCode::TpSwipeLeft; return true; }
            if (s == "TpSwipeRight") { out = TriggerCode::TpSwipeRight; return true; }

            return false;
        }
        bool TryParseAxisCode(std::string_view s, AxisCode& out)
        {
            if (s == "LStickX") { out = AxisCode::LStickX; return true; }
            if (s == "LStickY") { out = AxisCode::LStickY; return true; }
            if (s == "RStickX") { out = AxisCode::RStickX; return true; }
            if (s == "RStickY") { out = AxisCode::RStickY; return true; }
            if (s == "L2") { out = AxisCode::L2; return true; }
            if (s == "R2") { out = AxisCode::R2; return true; }
            return false;
        }
        // 支持两种写法：
        // 1) TP_LEFT_PRESS.Press
        // 2) Input.TP_LEFT_PRESS.Press
        bool TryParseBindingKey(std::string_view key, TriggerCode& code, TriggerPhase& phase)
        {
            if (StartsWith(key, "Input.")) {
                key.remove_prefix(6);
            }

            const auto pos = key.rfind('.');
            if (pos == std::string_view::npos) {
                return false;
            }

            const auto codeStr = key.substr(0, pos);
            const auto phaseStr = key.substr(pos + 1);

            return TryParseTriggerCode(codeStr, code) && TryParseTriggerPhase(phaseStr, phase);
        }

        bool TryParseAxisKey(std::string_view key, AxisCode& axis)
        {
            if (StartsWith(key, "InputAxis.")) {
                key.remove_prefix(10);
            }
            return TryParseAxisCode(key, axis);
        }

        bool ParseIniFile(
            const std::filesystem::path& path,
            std::vector<ActionRouter::BindingEntry>& out,
            std::vector<ActionRouter::AxisBindingEntry>& outAxis)
        {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return false;
            }

            out.clear();
            outAxis.clear();

            std::string line;
            std::size_t lineNo = 0;
            std::string section;  // "", "Bindings", "Axes", ...

            while (std::getline(ifs, line)) {
                ++lineNo;
                if (lineNo == 1 && line.size() >= 3 &&
                    static_cast<unsigned char>(line[0]) == 0xEF &&
                    static_cast<unsigned char>(line[1]) == 0xBB &&
                    static_cast<unsigned char>(line[2]) == 0xBF) {
                    line.erase(0, 3);
                }
                line = Trim(line);

                if (line.empty()) continue;
                if (line[0] == ';' || line[0] == '#') continue;

                // section header
                if (line.front() == '[' && line.back() == ']') {
                    section = Trim(line.substr(1, line.size() - 2));
                    continue;
                }

                const auto eq = line.find('=');
                if (eq == std::string::npos) {
                    logger::warn("[DualPad] Actions.ini:{} invalid line(no '='): {}", lineNo, line);
                    continue;
                }

                auto key = Trim(line.substr(0, eq));
                auto val = Trim(line.substr(eq + 1));
                if (key.empty() || val.empty()) {
                    continue;
                }

                // -------- [Axes] --------
                if (section == "Axes") {
                    AxisCode a{};
                    if (!TryParseAxisKey(key, a)) {
                        logger::warn("[DualPad] Actions.ini:{} invalid axis key: {}", lineNo, key);
                        continue;
                    }
                    outAxis.push_back(ActionRouter::AxisBindingEntry{ a, val });
                    continue;
                }

                // -------- [Bindings] or no section --------
                if (section.empty() || section == "Bindings") {
                    TriggerCode code{};
                    TriggerPhase phase{};
                    if (!TryParseBindingKey(key, code, phase)) {
                        logger::warn("[DualPad] Actions.ini:{} invalid trigger key: {}", lineNo, key);
                        continue;
                    }
                    out.push_back(ActionRouter::BindingEntry{ code, phase, val });
                    continue;
                }

                // unknown section -> ignore
            }

            return true;
        }
        std::filesystem::path DefaultConfigPath()
        {
            // Data/SKSE/Plugins/DualPadActions.ini （运行目录通常是游戏根目录）
            return std::filesystem::path("Data/SKSE/Plugins/DualPadActions.ini");
        }

        void WriteExampleIfMissing(const std::filesystem::path& path)
        {
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                return;
            }

            std::filesystem::create_directories(path.parent_path(), ec);

            std::ofstream ofs(path);
            if (!ofs.is_open()) {
                return;
            }

            ofs <<
                "; DualPad actions binding file\n"
                "; key format: <TriggerCode>.<Phase> = <ActionId>\n"
                "; or: Input.<TriggerCode>.<Phase> = <ActionId>\n"
                "[Bindings]\n"
                "TP_LEFT_PRESS.Press=" << actions::OpenInventory << "\n"
                "TP_RIGHT_PRESS.Press=" << actions::OpenMagic << "\n"
                "TP_SWIPE_UP.Pulse=" << actions::OpenMap << "\n"
                "TP_SWIPE_LEFT.Pulse=" << actions::OpenJournal << "\n"
                " \n"
                "[Axes]\n"
                "LStickX=" << actions::MoveX << "\n"
                "LStickY=" << actions::MoveY << "\n"
                "RStickX=" << actions::LookX << "\n"
                "RStickY=" << actions::LookY << "\n"
                "L2=" << actions::TriggerL << "\n"
                "R2=" << actions::TriggerR << "\n";
        }

        void TryReloadNow(bool forceLogNoFile = false)
        {
            if (!g_cfg.initialized) {
                return;
            }

            std::error_code ec;
            const bool exists = std::filesystem::exists(g_cfg.path, ec);
            if (!exists || ec) {
                if (forceLogNoFile) {
                    logger::warn("[DualPad] Actions.ini not found: {}", g_cfg.path.string());
                }
                return;
            }

            const auto wt = std::filesystem::last_write_time(g_cfg.path, ec);
            if (ec) {
                logger::warn("[DualPad] last_write_time failed: {}", g_cfg.path.string());
                return;
            }

            if (g_cfg.fileSeenOnce && wt == g_cfg.lastWrite) {
                return; // 没变化
            }

            std::vector<ActionRouter::BindingEntry> entries;
            std::vector<ActionRouter::AxisBindingEntry> axisEntries;
            if (!ParseIniFile(g_cfg.path, entries, axisEntries)) {
                logger::warn("[DualPad] Failed to parse Actions.ini: {}", g_cfg.path.string());
                return;
            }

            const auto bindingCount = entries.size();
            const auto axisCount = axisEntries.size();

            if (bindingCount == 0 && axisCount == 0) {
                logger::warn("[DualPad] Actions.ini parsed but no valid bindings, keep current bindings");
                g_cfg.lastWrite = wt;
                g_cfg.fileSeenOnce = true;
                return;
            }

            if (bindingCount > 0) {
                ActionRouter::GetSingleton().ReplaceBindings(std::move(entries));
            }
            if (axisCount > 0) {
                ActionRouter::GetSingleton().ReplaceAxisBindings(std::move(axisEntries));
            }

            g_cfg.lastWrite = wt;
            g_cfg.fileSeenOnce = true;

            logger::info("[DualPad] Actions.ini reloaded: {}", g_cfg.path.string());
            logger::info("[DualPad] Actions.ini parsed: bindings={}, axes={}", bindingCount, axisCount);
        }
    }

    void InitActionConfigHotReload(const std::filesystem::path& path)
    {
        logger::info("[DualPad] Enter InitActionConfigHotReload");
        g_cfg = HotReloadState{};
        g_cfg.path = path.empty() ? DefaultConfigPath() : path;
        g_cfg.initialized = true;
        g_cfg.nextPoll = std::chrono::steady_clock::now();

        WriteExampleIfMissing(g_cfg.path);

        // 启动时立刻尝试加载（若失败保留当前默认绑定）
        TryReloadNow(true);
    }

    void PollActionConfigHotReload()
    {
        if (!g_cfg.initialized) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now < g_cfg.nextPoll) {
            return;
        }

        g_cfg.nextPoll = now + kPollInterval;
        TryReloadNow(false);
    }
}