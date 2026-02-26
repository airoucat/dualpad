#include "pch.h"
#include "input/HidReader.h"
#include "input/DualSenseEdgeMapping.h"
#include "input/ActionRouter.h"
#include "input/StickFilter.h"
#include "input/AnalogState.h"

#include <hidapi/hidapi.h>
#include <RE/Skyrim.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <sstream>
#include <thread>

namespace logger = SKSE::log;
namespace dse = dualpad::input::dse;

// ============================================================================
// Log level control
// ============================================================================
// 0 = off all
// 1 = Error (default)
// 2 = Warn + Error
// 3 = Info + Warn + Error
#ifndef DUALPAD_LOG_LEVEL
#define DUALPAD_LOG_LEVEL 2
#endif

// Echo log to in-game console
#ifndef DUALPAD_LOG_ECHO_CONSOLE
#define DUALPAD_LOG_ECHO_CONSOLE 0
#endif

// Raw HID dump (very verbose)
#ifndef DUALPAD_DEBUG_RAW
#define DUALPAD_DEBUG_RAW 0
#endif

// Heartbeat log
#ifndef DUALPAD_HEARTBEAT
#define DUALPAD_HEARTBEAT 0
#endif

// Button edge logs
#ifndef DUALPAD_LOG_EDGE
#define DUALPAD_LOG_EDGE 0
#endif

// Touch split-press logs
#ifndef DUALPAD_LOG_TOUCH
#define DUALPAD_LOG_TOUCH 0
#endif

// Swipe logs
#ifndef DUALPAD_LOG_SWIPE
#define DUALPAD_LOG_SWIPE 0
#endif

namespace
{
    using dualpad::input::ActionRouter;
    using dualpad::input::TriggerCode;
    using dualpad::input::TriggerPhase;
    namespace stickf = dualpad::input::stick;
    using dualpad::input::AnalogState;

    // ===== touchpad feature toggles =====
    constexpr bool kEnableSplitTouchpadPress = true;   // true: Left/Mid/Right press; false: whole touchpad click
    constexpr bool kNoTouchClickFallbackMid = true;    // split mode: click with no finger -> Mid
    constexpr bool kEnableSwipe = true;                // touch swipe 4-dir
    constexpr int kSwipeThresholdX = 340;              // ~18% of 1920
    constexpr int kSwipeThresholdY = 190;              // ~18% of 1080

    // ===== stick channel toggles =====
    constexpr bool kPublishFilteredAnalog = true;      // continuous main channel
    constexpr bool kEnableStickDirectionTrigger = true; // discrete helper channel

    // DS4Windows-style direction hysteresis/dominance
    constexpr float kDirEnter = 0.58f;
    constexpr float kDirExit = 0.42f;
    constexpr float kDominance = 1.12f;

    enum TouchPressBits : std::uint8_t
    {
        kTpPressNone = 0,
        kTpPressLeft = 1 << 0,
        kTpPressMid = 1 << 1,
        kTpPressRight = 1 << 2
    };

    enum class StickDir : std::uint8_t
    {
        None = 0, Up, Down, Left, Right
    };

    std::atomic_bool g_running{ false };
    std::thread g_thread;

#if DUALPAD_LOG_ECHO_CONSOLE
    inline void ConsolePrint(const std::string& msg)
    {
        if (auto* c = RE::ConsoleLog::GetSingleton(); c) {
            c->Print("[DualPad] %s", msg.c_str());
        }
    }
#else
    inline void ConsolePrint(const std::string&) {}
#endif

#if DUALPAD_LOG_LEVEL >= 3
    template <class... Args>
    inline void DbgInfo(std::format_string<Args...> fmt, Args&&... args)
    {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        logger::info("[DualPad] {}", msg);
        ConsolePrint(msg);
    }
#else
    template <class... Args>
    inline void DbgInfo(std::format_string<Args...>, Args&&...) {}
#endif

#if DUALPAD_LOG_LEVEL >= 2
    template <class... Args>
    inline void DbgWarn(std::format_string<Args...> fmt, Args&&... args)
    {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        logger::warn("[DualPad] {}", msg);
        ConsolePrint(msg);
    }
#else
    template <class... Args>
    inline void DbgWarn(std::format_string<Args...>, Args&&...) {}
#endif

#if DUALPAD_LOG_LEVEL >= 1
    template <class... Args>
    inline void DbgError(std::format_string<Args...> fmt, Args&&... args)
    {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        logger::error("[DualPad] {}", msg);
        ConsolePrint(msg);
    }
#else
    template <class... Args>
    inline void DbgError(std::format_string<Args...>, Args&&...) {}
#endif

#if DUALPAD_DEBUG_RAW && (DUALPAD_LOG_LEVEL >= 3)
    std::string HexLine(const unsigned char* data, size_t len, size_t maxShow = 64)
    {
        std::ostringstream oss;
        const size_t n = (len < maxShow) ? len : maxShow;
        for (size_t i = 0; i < n; ++i) {
            oss << std::format("{:02X} ", data[i]);
        }
        return oss.str();
    }
#endif

    hid_device* TryOpenDualPad()
    {
        hid_device_info* infos = hid_enumerate(dse::kSonyVid, 0x0);
        hid_device_info* cur = infos;

        hid_device* dev = nullptr;
        while (cur) {
#if DUALPAD_DEBUG_RAW && (DUALPAD_LOG_LEVEL >= 3)
            DbgInfo("enum: VID={:04X} PID={:04X} iface={} usagePage=0x{:X} usage=0x{:X} path={}",
                cur->vendor_id, cur->product_id, cur->interface_number,
                cur->usage_page, cur->usage, (cur->path ? cur->path : "<null>"));
#endif
            if (dse::IsSupportedSonyPad(cur->vendor_id, cur->product_id) && cur->path) {
                dev = hid_open_path(cur->path);
                if (dev) {
                    hid_set_nonblocking(dev, 1);
                    DbgInfo("Open success: VID={:04X} PID={:04X} path={}",
                        cur->vendor_id, cur->product_id, cur->path);
                    break;
                }
            }
            cur = cur->next;
        }

        hid_free_enumeration(infos);
        return dev;
    }

    inline void LogEdge(std::uint8_t oldV, std::uint8_t newV, std::uint8_t mask, const char* name)
    {
#if DUALPAD_LOG_EDGE
        const bool oldOn = (oldV & mask) != 0;
        const bool newOn = (newV & mask) != 0;
        if (oldOn != newOn) {
            DbgInfo("{} {}", name, newOn ? "Pressed" : "Released");
        }
#else
        (void)oldV;
        (void)newV;
        (void)mask;
        (void)name;
#endif
    }

    inline void LogExtraEdge(std::uint8_t oldV, std::uint8_t newV, std::uint8_t mask, const char* name)
    {
#if DUALPAD_LOG_EDGE
        const bool oldOn = (oldV & mask) != 0;
        const bool newOn = (newV & mask) != 0;
        if (oldOn != newOn) {
            DbgInfo("{} {}", name, newOn ? "Pressed" : "Released");
        }
#else
        (void)oldV;
        (void)newV;
        (void)mask;
        (void)name;
#endif
    }

    inline void EmitTrigger(TriggerCode code, TriggerPhase phase)
    {
        ActionRouter::GetSingleton().EmitInput(code, phase);
    }

    inline void EmitEdge(std::uint8_t oldV, std::uint8_t newV, std::uint8_t mask, TriggerCode code)
    {
        const bool oldOn = (oldV & mask) != 0;
        const bool newOn = (newV & mask) != 0;
        if (oldOn == newOn) {
            return;
        }
        EmitTrigger(code, newOn ? TriggerPhase::Press : TriggerPhase::Release);
    }

    inline TriggerCode DpadNibbleToCode(std::uint8_t dpadNibble)
    {
        switch (dpadNibble & 0x0F) {
        case 0: return TriggerCode::DpadUp;
        case 1: return TriggerCode::DpadUpRight;
        case 2: return TriggerCode::DpadRight;
        case 3: return TriggerCode::DpadDownRight;
        case 4: return TriggerCode::DpadDown;
        case 5: return TriggerCode::DpadDownLeft;
        case 6: return TriggerCode::DpadLeft;
        case 7: return TriggerCode::DpadUpLeft;
        default: return TriggerCode::None;  // 8 = neutral
        }
    }

    inline TriggerCode TouchPressBitToCode(std::uint8_t bit)
    {
        switch (bit) {
        case kTpPressLeft: return TriggerCode::TpLeftPress;
        case kTpPressMid: return TriggerCode::TpMidPress;
        case kTpPressRight: return TriggerCode::TpRightPress;
        default: return TriggerCode::None;
        }
    }

    inline const char* TouchPressName(std::uint8_t bit)
    {
        switch (bit) {
        case kTpPressLeft: return "TouchpadLeftPress";
        case kTpPressMid: return "TouchpadMidPress";
        case kTpPressRight: return "TouchpadRightPress";
        default: return "TouchpadPressUnknown";
        }
    }

    inline std::uint8_t ClassifyTouchpadRegion(const dse::State& s)
    {
        if (s.hasTouchData && s.tp1.active) {
            const auto x = s.tp1.x;
            if (x < 640) return kTpPressLeft;
            if (x < 1280) return kTpPressMid;
            return kTpPressRight;
        }
        return kNoTouchClickFallbackMid ? kTpPressMid : kTpPressNone;
    }

    inline TriggerCode LDirCode(StickDir d)
    {
        switch (d) {
        case StickDir::Up: return TriggerCode::LStickUp;
        case StickDir::Down: return TriggerCode::LStickDown;
        case StickDir::Left: return TriggerCode::LStickLeft;
        case StickDir::Right: return TriggerCode::LStickRight;
        default: return TriggerCode::None;
        }
    }

    inline TriggerCode RDirCode(StickDir d)
    {
        switch (d) {
        case StickDir::Up: return TriggerCode::RStickUp;
        case StickDir::Down: return TriggerCode::RStickDown;
        case StickDir::Left: return TriggerCode::RStickLeft;
        case StickDir::Right: return TriggerCode::RStickRight;
        default: return TriggerCode::None;
        }
    }

    inline StickDir ResolveDir(float x, float y, StickDir prev)
    {
        const float ax = std::fabs(x);
        const float ay = std::fabs(y);
        const float m = (ax > ay) ? ax : ay;

        if (prev == StickDir::None) {
            if (m < kDirEnter) return StickDir::None;
        }
        else {
            if (m < kDirExit) return StickDir::None;
        }

        if (ax > ay * kDominance) return (x >= 0.0f) ? StickDir::Right : StickDir::Left;
        if (ay > ax * kDominance) return (y >= 0.0f) ? StickDir::Up : StickDir::Down;

        return (ax >= ay) ? ((x >= 0.0f) ? StickDir::Right : StickDir::Left)
            : ((y >= 0.0f) ? StickDir::Up : StickDir::Down);
    }

    inline void EmitDirChange(StickDir oldD, StickDir newD, bool leftStick)
    {
        if (oldD == newD) return;

        const auto oldCode = leftStick ? LDirCode(oldD) : RDirCode(oldD);
        const auto newCode = leftStick ? LDirCode(newD) : RDirCode(newD);

        if (oldCode != TriggerCode::None) EmitTrigger(oldCode, TriggerPhase::Release);
        if (newCode != TriggerCode::None) EmitTrigger(newCode, TriggerPhase::Press);
    }

    struct TouchRuntime
    {
        std::uint8_t heldSplitPress{ kTpPressNone };
        bool swipeTracking{ false };
        int startX{ 0 }, startY{ 0 };
        int lastX{ 0 }, lastY{ 0 };
        bool suppressSwipeThisTouch{ false };
    };

    struct StickRuntime
    {
        stickf::Filter l;
        stickf::Filter r;
        StickDir prevL{ StickDir::None };
        StickDir prevR{ StickDir::None };

        StickRuntime()
        {
            stickf::Config lc;
            lc.deadzone = 0.05f;
            lc.antiDeadzone = 0.10f;   // 提升小幅输入可感知性
            lc.maxzone = 1.0f;
            lc.expo = 1.00f;           // 先关指数曲线
            lc.smoothAlpha = 0.85f;    // 降低“粘滞感”
            lc.epsilon = 0.001f;
            lc.invertY = true;
            stickf::Config rc = lc;
            rc.deadzone = 0.05f;
            rc.expo = 1.00f;
            rc.smoothAlpha = 0.85f;

            l.SetConfig(lc);
            r.SetConfig(rc);
        }
    };

    inline void HandleStickChannels(const dse::State& cur, StickRuntime& rt, bool emitDir)
    {
        const auto lv = rt.l.ProcessRawU8(cur.lx, cur.ly);
        const auto rv = rt.r.ProcessRawU8(cur.rx, cur.ry);

        if (kPublishFilteredAnalog) {
            AnalogState::GetSingleton().Update(
                lv.x, lv.y, rv.x, rv.y,
                static_cast<float>(cur.l2) / 255.0f,
                static_cast<float>(cur.r2) / 255.0f
            );
        }

        if (!kEnableStickDirectionTrigger) return;

        const auto nl = ResolveDir(lv.x, lv.y, rt.prevL);
        const auto nr = ResolveDir(rv.x, rv.y, rt.prevR);

        if (emitDir) {
            EmitDirChange(rt.prevL, nl, true);
            EmitDirChange(rt.prevR, nr, false);
        }

        rt.prevL = nl;
        rt.prevR = nr;
    }

    // TODO: future trajectory sample for menu selector/cursor
    inline void OnTouchTrajectorySample(std::uint16_t x, std::uint16_t y, bool active)
    {
        (void)x;
        (void)y;
        (void)active;
    }

    void EmitStateDiffTriggers(const dse::State& prev, const dse::State& cur)
    {
        // DPad special: release old direction + press new direction
        const auto oldD = static_cast<std::uint8_t>(prev.btn0 & dse::kDpadMask);
        const auto newD = static_cast<std::uint8_t>(cur.btn0 & dse::kDpadMask);
        if (oldD != newD) {
            const auto oldCode = DpadNibbleToCode(oldD);
            const auto newCode = DpadNibbleToCode(newD);
            if (oldCode != TriggerCode::None) {
                EmitTrigger(oldCode, TriggerPhase::Release);
            }
            if (newCode != TriggerCode::None) {
                EmitTrigger(newCode, TriggerPhase::Press);
            }
        }

        EmitEdge(prev.btn0, cur.btn0, dse::kSquare, TriggerCode::Square);
        EmitEdge(prev.btn0, cur.btn0, dse::kCross, TriggerCode::Cross);
        EmitEdge(prev.btn0, cur.btn0, dse::kCircle, TriggerCode::Circle);
        EmitEdge(prev.btn0, cur.btn0, dse::kTriangle, TriggerCode::Triangle);

        EmitEdge(prev.btn1, cur.btn1, dse::kL1, TriggerCode::L1);
        EmitEdge(prev.btn1, cur.btn1, dse::kR1, TriggerCode::R1);
        EmitEdge(prev.btn1, cur.btn1, dse::kL2Button, TriggerCode::L2Button);
        EmitEdge(prev.btn1, cur.btn1, dse::kR2Button, TriggerCode::R2Button);
        EmitEdge(prev.btn1, cur.btn1, dse::kCreate, TriggerCode::Create);
        EmitEdge(prev.btn1, cur.btn1, dse::kOptions, TriggerCode::Options);
        EmitEdge(prev.btn1, cur.btn1, dse::kL3, TriggerCode::L3);
        EmitEdge(prev.btn1, cur.btn1, dse::kR3, TriggerCode::R3);

        EmitEdge(prev.btn2, cur.btn2, dse::kPS, TriggerCode::PS);
        if (!kEnableSplitTouchpadPress) {
            EmitEdge(prev.btn2, cur.btn2, dse::kTouchpadClick, TriggerCode::TouchpadClick);
        }
        EmitEdge(prev.btn2, cur.btn2, dse::kMic, TriggerCode::Mic);

        EmitEdge(prev.dseExtra, cur.dseExtra, dse::kExtraFnLeft, TriggerCode::FnLeft);
        EmitEdge(prev.dseExtra, cur.dseExtra, dse::kExtraFnRight, TriggerCode::FnRight);
        EmitEdge(prev.dseExtra, cur.dseExtra, dse::kExtraBackLeft, TriggerCode::BackLeft);
        EmitEdge(prev.dseExtra, cur.dseExtra, dse::kExtraBackRight, TriggerCode::BackRight);
    }

    void HandleTouchpadAdvanced(const dse::State& prev, const dse::State& cur, TouchRuntime& rt)
    {
        // ---- split press (mutually exclusive with whole touchpad click) ----
        const bool prevClick = (prev.btn2 & dse::kTouchpadClick) != 0;
        const bool curClick = (cur.btn2 & dse::kTouchpadClick) != 0;

        if (kEnableSplitTouchpadPress) {
            if (!prevClick && curClick) {
                rt.heldSplitPress = ClassifyTouchpadRegion(cur);
                if (rt.heldSplitPress != kTpPressNone) {
                    const auto code = TouchPressBitToCode(rt.heldSplitPress);
                    if (code != TriggerCode::None) {
                        EmitTrigger(code, TriggerPhase::Press);
                    }
#if DUALPAD_LOG_TOUCH
                    DbgInfo("{} Pressed", TouchPressName(rt.heldSplitPress));
#endif
                }
                if (rt.swipeTracking) {
                    rt.suppressSwipeThisTouch = true;
                }
            }
            else if (prevClick && !curClick) {
                if (rt.heldSplitPress != kTpPressNone) {
                    const auto code = TouchPressBitToCode(rt.heldSplitPress);
                    if (code != TriggerCode::None) {
                        EmitTrigger(code, TriggerPhase::Release);
                    }
#if DUALPAD_LOG_TOUCH
                    DbgInfo("{} Released", TouchPressName(rt.heldSplitPress));
#endif
                    rt.heldSplitPress = kTpPressNone;
                }
            }
        }

        // ---- swipe (touch precondition, trigger on finger up) ----
        if (!kEnableSwipe) {
            return;
        }

        const bool prevTouch = prev.hasTouchData && prev.tp1.active;
        const bool curTouch = cur.hasTouchData && cur.tp1.active;

        // touch down: start tracking
        if (!prevTouch && curTouch) {
            rt.swipeTracking = true;
            rt.startX = static_cast<int>(cur.tp1.x);
            rt.startY = static_cast<int>(cur.tp1.y);
            rt.lastX = rt.startX;
            rt.lastY = rt.startY;
            rt.suppressSwipeThisTouch = false;
            OnTouchTrajectorySample(cur.tp1.x, cur.tp1.y, true);
            return;
        }

        // touch move: update last point
        if (prevTouch && curTouch) {
            rt.lastX = static_cast<int>(cur.tp1.x);
            rt.lastY = static_cast<int>(cur.tp1.y);
            OnTouchTrajectorySample(cur.tp1.x, cur.tp1.y, true);
            return;
        }

        // finger up: evaluate once
        if (prevTouch && !curTouch) {
            if (rt.swipeTracking && !rt.suppressSwipeThisTouch) {
                const int dx = rt.lastX - rt.startX;
                const int dy = rt.lastY - rt.startY;
                if (std::abs(dx) >= kSwipeThresholdX || std::abs(dy) >= kSwipeThresholdY) {
                    if (std::abs(dx) >= std::abs(dy)) {
                        if (dx >= 0) {
                            EmitTrigger(TriggerCode::TpSwipeRight, TriggerPhase::Pulse);
#if DUALPAD_LOG_SWIPE
                            DbgInfo("TouchpadSwipe Right");
#endif
                        }
                        else {
                            EmitTrigger(TriggerCode::TpSwipeLeft, TriggerPhase::Pulse);
#if DUALPAD_LOG_SWIPE
                            DbgInfo("TouchpadSwipe Left");
#endif
                        }
                    }
                    else {
                        if (dy >= 0) {
                            EmitTrigger(TriggerCode::TpSwipeDown, TriggerPhase::Pulse);
#if DUALPAD_LOG_SWIPE
                            DbgInfo("TouchpadSwipe Down");
#endif
                        }
                        else {
                            EmitTrigger(TriggerCode::TpSwipeUp, TriggerPhase::Pulse);
#if DUALPAD_LOG_SWIPE
                            DbgInfo("TouchpadSwipe Up");
#endif
                        }
                    }
                }
            }
            rt.swipeTracking = false;
            OnTouchTrajectorySample(0, 0, false);
        }
    }

    void LogStateDiff(const dse::State& prev, const dse::State& cur)
    {
#if DUALPAD_LOG_EDGE
        const auto oldD = static_cast<std::uint8_t>(prev.btn0 & dse::kDpadMask);
        const auto newD = static_cast<std::uint8_t>(cur.btn0 & dse::kDpadMask);
        if (oldD != newD) {
            DbgInfo("DPad {} -> {}", dse::DpadToString(oldD), dse::DpadToString(newD));
        }
#else
        (void)prev;
        (void)cur;
#endif

        LogEdge(prev.btn0, cur.btn0, dse::kSquare, "Square");
        LogEdge(prev.btn0, cur.btn0, dse::kCross, "Cross");
        LogEdge(prev.btn0, cur.btn0, dse::kCircle, "Circle");
        LogEdge(prev.btn0, cur.btn0, dse::kTriangle, "Triangle");

        LogEdge(prev.btn1, cur.btn1, dse::kL1, "L1");
        LogEdge(prev.btn1, cur.btn1, dse::kR1, "R1");
        LogEdge(prev.btn1, cur.btn1, dse::kL2Button, "L2_Button");
        LogEdge(prev.btn1, cur.btn1, dse::kR2Button, "R2_Button");
        LogEdge(prev.btn1, cur.btn1, dse::kCreate, "Create");
        LogEdge(prev.btn1, cur.btn1, dse::kOptions, "Options");
        LogEdge(prev.btn1, cur.btn1, dse::kL3, "L3");
        LogEdge(prev.btn1, cur.btn1, dse::kR3, "R3");

        LogEdge(prev.btn2, cur.btn2, dse::kPS, "PS");
        if (!kEnableSplitTouchpadPress) {
            LogEdge(prev.btn2, cur.btn2, dse::kTouchpadClick, "TouchpadClick");
        }
        LogEdge(prev.btn2, cur.btn2, dse::kMic, "Mic");

        LogExtraEdge(prev.dseExtra, cur.dseExtra, dse::kExtraFnLeft, "FnLeft");
        LogExtraEdge(prev.dseExtra, cur.dseExtra, dse::kExtraFnRight, "FnRight");
        LogExtraEdge(prev.dseExtra, cur.dseExtra, dse::kExtraBackLeft, "BackLeft");
        LogExtraEdge(prev.dseExtra, cur.dseExtra, dse::kExtraBackRight, "BackRight");
    }

    void ReaderLoop()
    {
        DbgInfo("ReaderLoop started");

        if (hid_init() != 0) {
            DbgError("hid_init failed");
            return;
        }

        hid_device* dev = nullptr;
        dse::State prev{};
        bool hasPrev = false;
        TouchRuntime touchRt{};
        StickRuntime stickRt{};

#if DUALPAD_HEARTBEAT && (DUALPAD_LOG_LEVEL >= 3)
        auto lastHeartbeat = std::chrono::steady_clock::now();
#endif

        while (g_running.load()) {
            if (!dev) {
                dev = TryOpenDualPad();
                if (!dev) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    continue;
                }
                hasPrev = false;
                touchRt = TouchRuntime{};
                stickRt = StickRuntime{};
                AnalogState::GetSingleton().Reset();
                DbgInfo("DualPad HID connected.");
            }

            unsigned char buf[128]{};
            const int n = hid_read_timeout(dev, buf, sizeof(buf), 8);

            if (n > 0) {
#if DUALPAD_DEBUG_RAW && (DUALPAD_LOG_LEVEL >= 3)
                DbgInfo("[RAW] len={} data={}", n, HexLine(buf, static_cast<size_t>(n)));
#endif
                dse::State cur{};

                if (dse::ParseReport01(buf, n, cur)) {
                    if (!hasPrev) {
                        // warmup continuous channel (no direction edge)
                        HandleStickChannels(cur, stickRt, false);

                        prev = cur;
                        hasPrev = true;
                        DbgInfo("DS init: LX={} LY={} RX={} RY={} L2={} R2={} btn0={:02X} btn1={:02X} btn2={:02X} btn3={:02X}",
                            cur.lx, cur.ly, cur.rx, cur.ry, cur.l2, cur.r2, cur.btn0, cur.btn1, cur.btn2, cur.btn3);
                    }
                    else {
                        // every frame: continuous analog + optional dir edges
                        HandleStickChannels(cur, stickRt, true);

                        const bool buttonChanged =
                            (cur.btn0 != prev.btn0) ||
                            (cur.btn1 != prev.btn1) ||
                            (cur.btn2 != prev.btn2) ||
                            (cur.btn3 != prev.btn3) ||
                            (cur.dseExtra != prev.dseExtra);

                        const bool touchChanged =
                            (cur.hasTouchData != prev.hasTouchData) ||
                            (cur.tp1.active != prev.tp1.active) ||
                            (cur.tp1.x != prev.tp1.x) ||
                            (cur.tp1.y != prev.tp1.y);

                        if (buttonChanged) {
                            LogStateDiff(prev, cur);
                            EmitStateDiffTriggers(prev, cur);
                        }

                        if (buttonChanged || touchChanged) {
                            HandleTouchpadAdvanced(prev, cur, touchRt);
                        }

                        prev = cur;
                    }
                }
#if DUALPAD_DEBUG_RAW && (DUALPAD_LOG_LEVEL >= 3)
                else {
                    DbgInfo("Ignore report id={:02X}, len={}", buf[0], n);
                }
#endif
            }
            else if (n < 0) {
                DbgWarn("hid_read_timeout error, reopening...");
                hid_close(dev);
                dev = nullptr;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

#if DUALPAD_HEARTBEAT && (DUALPAD_LOG_LEVEL >= 3)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastHeartbeat > std::chrono::seconds(3)) {
                    DbgInfo("reader heartbeat alive");
                    lastHeartbeat = now;
                }
            }
#endif
        }

        if (dev) {
            hid_close(dev);
            dev = nullptr;
        }

        hid_exit();
        DbgInfo("HID reader stopped");
    }
}

namespace dualpad::input
{
    void StartHidReader()
    {
        if (g_running.exchange(true)) {
            DbgWarn("StartHidReader ignored: already running");
            return;
        }
        g_thread = std::thread(ReaderLoop);
        DbgInfo("HID reader thread started");
    }

    void StopHidReader()
    {
        if (!g_running.exchange(false)) {
            return;
        }
        if (g_thread.joinable()) {
            g_thread.join();
        }
    }
}