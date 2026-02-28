#pragma once
#include <cstdint>

namespace dualpad::input::dse
{
    // Sony VID/PID
    inline constexpr std::uint16_t kSonyVid = 0x054C;
    inline constexpr std::uint16_t kPidDualSense = 0x0CE6;
    inline constexpr std::uint16_t kPidDualSenseEdge = 0x0DF2;

    // Report 0x01 按键位定义
    namespace btn
    {
        // btn0 (buf[8])
        inline constexpr std::uint8_t kDpadMask = 0x0F;
        inline constexpr std::uint8_t kSquare = 0x10;
        inline constexpr std::uint8_t kCross = 0x20;
        inline constexpr std::uint8_t kCircle = 0x40;
        inline constexpr std::uint8_t kTriangle = 0x80;

        // btn1 (buf[9])
        inline constexpr std::uint8_t kL1 = 0x01;
        inline constexpr std::uint8_t kR1 = 0x02;
        inline constexpr std::uint8_t kL2Button = 0x04;
        inline constexpr std::uint8_t kR2Button = 0x08;
        inline constexpr std::uint8_t kCreate = 0x10;
        inline constexpr std::uint8_t kOptions = 0x20;
        inline constexpr std::uint8_t kL3 = 0x40;
        inline constexpr std::uint8_t kR3 = 0x80;

        // btn2 (buf[10])
        inline constexpr std::uint8_t kPS = 0x01;
        inline constexpr std::uint8_t kTouchpadClick = 0x02;
        inline constexpr std::uint8_t kMic = 0x04;
    }

    // DSE 扩展按键
    enum ExtraBits : std::uint8_t
    {
        kExtraNone = 0,
        kExtraFnLeft = 1 << 0,
        kExtraFnRight = 1 << 1,
        kExtraBackLeft = 1 << 2,
        kExtraBackRight = 1 << 3
    };

    inline std::uint8_t DecodeExtras(std::uint8_t btn2, std::uint8_t btn3)
    {
        std::uint8_t out = kExtraNone;
        if (btn2 & 0x10) out |= kExtraFnLeft;
        if (btn2 & 0x20) out |= kExtraFnRight;
        if (btn2 & 0x40) out |= kExtraBackLeft;
        if (btn2 & 0x80) out |= kExtraBackRight;

        if (btn3 & 0x01) out |= kExtraFnLeft;
        if (btn3 & 0x02) out |= kExtraFnRight;
        if (btn3 & 0x04) out |= kExtraBackLeft;
        if (btn3 & 0x08) out |= kExtraBackRight;

        return out;
    }

    // 触摸点
    struct TouchPoint
    {
        bool active{ false };
        std::uint8_t id{ 0 };
        std::uint16_t x{ 0 };  // 0-1919
        std::uint16_t y{ 0 };  // 0-1079
    };

    inline TouchPoint ParseTouchPoint(const unsigned char* p4)
    {
        TouchPoint t{};
        const std::uint8_t b0 = p4[0];
        t.active = (b0 & 0x80) == 0;
        t.id = static_cast<std::uint8_t>(b0 & 0x7F);
        t.x = static_cast<std::uint16_t>(p4[1] | ((p4[2] & 0x0F) << 8));
        t.y = static_cast<std::uint16_t>(((p4[2] & 0xF0) >> 4) | (p4[3] << 4));
        return t;
    }

    // 完整状态
    struct State
    {
        std::uint8_t lx{ 0 }, ly{ 0 }, rx{ 0 }, ry{ 0 };
        std::uint8_t l2{ 0 }, r2{ 0 };
        std::uint8_t btn0{ 0 }, btn1{ 0 }, btn2{ 0 };
        std::uint8_t extraButtons{ 0 };

        TouchPoint touch1{};
        TouchPoint touch2{};
        bool hasTouchData{ false };
    };

    inline bool ParseReport01(const unsigned char* buf, int len, State& out)
    {
        if (!buf || len < 11 || buf[0] != 0x01) {
            return false;
        }

        out.lx = buf[1];
        out.ly = buf[2];
        out.rx = buf[3];
        out.ry = buf[4];
        out.l2 = buf[5];
        out.r2 = buf[6];
        out.btn0 = buf[8];
        out.btn1 = buf[9];
        out.btn2 = buf[10];

        const std::uint8_t btn3 = (len > 11) ? buf[11] : 0;
        out.extraButtons = DecodeExtras(out.btn2, btn3);

        if (len >= 37) {
            out.touch1 = ParseTouchPoint(buf + 33);
            out.hasTouchData = true;
        }
        if (len >= 41) {
            out.touch2 = ParseTouchPoint(buf + 37);
        }

        return true;
    }
}