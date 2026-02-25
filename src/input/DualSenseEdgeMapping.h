#pragma once

#include <cstdint>

namespace dualpad::input::dse
{
    // Sony VID / common PID
    inline constexpr std::uint16_t kSonyVid = 0x054C;
    inline constexpr std::uint16_t kPidDs4 = 0x05C4;
    inline constexpr std::uint16_t kPidDs4v2 = 0x09CC;
    inline constexpr std::uint16_t kPidDs4Dongle = 0x0BA0;
    inline constexpr std::uint16_t kPidDualSense = 0x0CE6;
    inline constexpr std::uint16_t kPidDualSenseEdge = 0x0DF2;  // one common PID

    inline bool IsSupportedSonyPad(std::uint16_t vid, std::uint16_t pid)
    {
        if (vid != kSonyVid) {
            return false;
        }

        switch (pid) {
        case kPidDs4:
        case kPidDs4v2:
        case kPidDs4Dongle:
        case kPidDualSense:
        case kPidDualSenseEdge:
            return true;
        default:
            return false;
        }
    }

    // -------- DualSense report 0x01 buttons --------
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

    // DSE extra keys (canonical bits)
    enum ExtraBits : std::uint8_t
    {
        kExtraNone = 0,
        kExtraFnLeft = 1 << 0,
        kExtraFnRight = 1 << 1,
        kExtraBackLeft = 1 << 2,
        kExtraBackRight = 1 << 3
    };

    inline std::uint8_t DecodeDseExtrasFromBtn2(std::uint8_t btn2)
    {
        std::uint8_t out = kExtraNone;
        if (btn2 & 0x10) out |= kExtraFnLeft;
        if (btn2 & 0x20) out |= kExtraFnRight;
        if (btn2 & 0x40) out |= kExtraBackLeft;
        if (btn2 & 0x80) out |= kExtraBackRight;
        return out;
    }

    inline std::uint8_t DecodeDseExtrasFromBtn3(std::uint8_t btn3)
    {
        std::uint8_t out = kExtraNone;
        if (btn3 & 0x01) out |= kExtraFnLeft;
        if (btn3 & 0x02) out |= kExtraFnRight;
        if (btn3 & 0x04) out |= kExtraBackLeft;
        if (btn3 & 0x08) out |= kExtraBackRight;
        return out;
    }

    inline const char* DpadToString(std::uint8_t v)
    {
        switch (v & 0x0F) {
        case 0: return "Up";
        case 1: return "UpRight";
        case 2: return "Right";
        case 3: return "DownRight";
        case 4: return "Down";
        case 5: return "DownLeft";
        case 6: return "Left";
        case 7: return "UpLeft";
        case 8: return "Neutral";
        default: return "Unknown";
        }
    }

    // Touch decode assumptions for DualSense report 0x01 (USB common layout)
    inline constexpr int kTouch1Offset = 33; // bytes: [33..36]
    inline constexpr int kTouch2Offset = 37; // bytes: [37..40]
    inline constexpr std::uint16_t kTouchMaxX = 1919;
    inline constexpr std::uint16_t kTouchMaxY = 1079;

    struct TouchPoint
    {
        bool active{ false };      // false = no finger
        std::uint8_t id{ 0 };
        std::uint16_t x{ 0 };      // 0..1919
        std::uint16_t y{ 0 };      // 0..1079
    };

    inline TouchPoint ParseTouchPoint(const unsigned char* p4)
    {
        TouchPoint t{};
        const std::uint8_t b0 = p4[0];
        t.active = (b0 & 0x80) == 0; // bit7=1 means inactive
        t.id = static_cast<std::uint8_t>(b0 & 0x7F);

        // DS style 12-bit coords
        t.x = static_cast<std::uint16_t>(p4[1] | ((p4[2] & 0x0F) << 8));
        t.y = static_cast<std::uint16_t>(((p4[2] & 0xF0) >> 4) | (p4[3] << 4));
        return t;
    }

    struct State
    {
        std::uint8_t reportId{ 0 };
        std::uint8_t lx{ 0 }, ly{ 0 }, rx{ 0 }, ry{ 0 };
        std::uint8_t l2{ 0 }, r2{ 0 };
        std::uint8_t btn0{ 0 }, btn1{ 0 }, btn2{ 0 }, btn3{ 0 };
        std::uint8_t dseExtra{ 0 };

        bool hasTouchData{ false };
        TouchPoint tp1{};
        TouchPoint tp2{};

        bool valid{ false };
    };

    inline bool ParseReport01(const unsigned char* buf, int n, State& s)
    {
        if (!buf || n < 11) {
            return false;
        }
        if (buf[0] != 0x01) {
            return false;
        }

        s.reportId = buf[0];
        s.lx = buf[1];
        s.ly = buf[2];
        s.rx = buf[3];
        s.ry = buf[4];
        s.l2 = buf[5];
        s.r2 = buf[6];
        s.btn0 = buf[8];
        s.btn1 = buf[9];
        s.btn2 = buf[10];
        s.btn3 = (n > 11) ? buf[11] : 0;

        const auto e2 = DecodeDseExtrasFromBtn2(s.btn2);
        const auto e3 = DecodeDseExtrasFromBtn3(s.btn3);
        s.dseExtra = static_cast<std::uint8_t>(e2 | e3);

        s.hasTouchData = false;
        if (n >= (kTouch1Offset + 4)) {
            s.tp1 = ParseTouchPoint(buf + kTouch1Offset);
            s.hasTouchData = true;
        }
        if (n >= (kTouch2Offset + 4)) {
            s.tp2 = ParseTouchPoint(buf + kTouch2Offset);
            s.hasTouchData = true;
        }

        s.valid = true;
        return true;
    }
}