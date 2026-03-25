#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace dualpad::input::ini
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

    inline std::string ToLower(std::string s)
    {
        std::transform(
            s.begin(),
            s.end(),
            s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    inline bool ParseBool(std::string_view value, bool defaultValue = false)
    {
        const auto normalized = ToLower(Trim(std::string(value)));
        if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
            return true;
        }
        if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
            return false;
        }
        return defaultValue;
    }

    inline void StripUtf8Bom(std::string& line)
    {
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
    }
}
