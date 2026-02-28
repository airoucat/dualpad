#include "pch.h"
#include "input/HidReader.h"
#include "input/DualSenseProtocol.h"
#include "input/SyntheticPadState.h"
#include "input/TouchpadGesture.h"
#include "input/PadProfile.h"
#include "input/Trigger.h"
#include "input/BindingManager.h"
#include "input/ActionExecutor.h"
#include "input/InputContext.h"

#include <SKSE/SKSE.h>
#include <hidapi/hidapi.h>
#include <thread>
#include <atomic>
#include <chrono>

namespace logger = SKSE::log;
namespace dse = dualpad::input::dse;

namespace
{
    std::atomic_bool g_running{ false };
    std::thread g_thread;

    using namespace std::chrono_literals;

    // 按键码映射
    enum class BtnCode : std::uint32_t
    {
        Square = 0x00000001,
        Cross = 0x00000002,
        Circle = 0x00000004,
        Triangle = 0x00000008,

        L1 = 0x00000010,
        R1 = 0x00000020,
        L2Button = 0x00000040,
        R2Button = 0x00000080,

        Create = 0x00000100,
        Options = 0x00000200,
        L3 = 0x00000400,
        R3 = 0x00000800,

        PS = 0x00001000,
        Mic = 0x00002000,
        TouchpadClick = 0x00004000,

        DpadUp = 0x00010000,
        DpadDown = 0x00020000,
        DpadLeft = 0x00040000,
        DpadRight = 0x00080000,

        FnLeft = 0x00100000,
        FnRight = 0x00200000,
        BackLeft = 0x00400000,
        BackRight = 0x00800000,

        TpLeftPress = 0x01000000,
        TpMidPress = 0x02000000,
        TpRightPress = 0x04000000,

        TpSwipeUp = 0x08000000,
        TpSwipeDown = 0x10000000,
        TpSwipeLeft = 0x20000000,
        TpSwipeRight = 0x40000000
    };

    inline std::uint32_t BuildButtonMask(const dse::State& state)
    {
        std::uint32_t mask = 0;

        // 面键
        if (state.btn0 & dse::btn::kSquare) mask |= static_cast<std::uint32_t>(BtnCode::Square);
        if (state.btn0 & dse::btn::kCross) mask |= static_cast<std::uint32_t>(BtnCode::Cross);
        if (state.btn0 & dse::btn::kCircle) mask |= static_cast<std::uint32_t>(BtnCode::Circle);
        if (state.btn0 & dse::btn::kTriangle) mask |= static_cast<std::uint32_t>(BtnCode::Triangle);

        // 肩键
        if (state.btn1 & dse::btn::kL1) mask |= static_cast<std::uint32_t>(BtnCode::L1);
        if (state.btn1 & dse::btn::kR1) mask |= static_cast<std::uint32_t>(BtnCode::R1);
        if (state.btn1 & dse::btn::kL2Button) mask |= static_cast<std::uint32_t>(BtnCode::L2Button);
        if (state.btn1 & dse::btn::kR2Button) mask |= static_cast<std::uint32_t>(BtnCode::R2Button);

        // 功能键
        if (state.btn1 & dse::btn::kCreate) mask |= static_cast<std::uint32_t>(BtnCode::Create);
        if (state.btn1 & dse::btn::kOptions) mask |= static_cast<std::uint32_t>(BtnCode::Options);
        if (state.btn1 & dse::btn::kL3) mask |= static_cast<std::uint32_t>(BtnCode::L3);
        if (state.btn1 & dse::btn::kR3) mask |= static_cast<std::uint32_t>(BtnCode::R3);

        if (state.btn2 & dse::btn::kPS) mask |= static_cast<std::uint32_t>(BtnCode::PS);
        if (state.btn2 & dse::btn::kMic) mask |= static_cast<std::uint32_t>(BtnCode::Mic);

        // D-Pad
        const std::uint8_t dpad = state.btn0 & dse::btn::kDpadMask;
        if (dpad == 0 || dpad == 1 || dpad == 7) mask |= static_cast<std::uint32_t>(BtnCode::DpadUp);
        if (dpad == 1 || dpad == 2 || dpad == 3) mask |= static_cast<std::uint32_t>(BtnCode::DpadRight);
        if (dpad == 3 || dpad == 4 || dpad == 5) mask |= static_cast<std::uint32_t>(BtnCode::DpadDown);
        if (dpad == 5 || dpad == 6 || dpad == 7) mask |= static_cast<std::uint32_t>(BtnCode::DpadLeft);

        // DSE 扩展
        if (state.extraButtons & dse::kExtraFnLeft) mask |= static_cast<std::uint32_t>(BtnCode::FnLeft);
        if (state.extraButtons & dse::kExtraFnRight) mask |= static_cast<std::uint32_t>(BtnCode::FnRight);
        if (state.extraButtons & dse::kExtraBackLeft) mask |= static_cast<std::uint32_t>(BtnCode::BackLeft);
        if (state.extraButtons & dse::kExtraBackRight) mask |= static_cast<std::uint32_t>(BtnCode::BackRight);

        return mask;
    }

    inline std::uint32_t GestureToMask(dualpad::input::TouchGesture g)
    {
        using TG = dualpad::input::TouchGesture;
        switch (g) {
        case TG::LeftPress: return static_cast<std::uint32_t>(BtnCode::TpLeftPress);
        case TG::MidPress: return static_cast<std::uint32_t>(BtnCode::TpMidPress);
        case TG::RightPress: return static_cast<std::uint32_t>(BtnCode::TpRightPress);
        case TG::SwipeUp: return static_cast<std::uint32_t>(BtnCode::TpSwipeUp);
        case TG::SwipeDown: return static_cast<std::uint32_t>(BtnCode::TpSwipeDown);
        case TG::SwipeLeft: return static_cast<std::uint32_t>(BtnCode::TpSwipeLeft);
        case TG::SwipeRight: return static_cast<std::uint32_t>(BtnCode::TpSwipeRight);
        default: return 0;
        }
    }

    inline float NormalizeU8(std::uint8_t v)
    {
        return (static_cast<float>(v) - 127.5f) / 127.5f;
    }

    hid_device* TryOpenDualSense()
    {
        hid_device_info* infos = hid_enumerate(dse::kSonyVid, 0x0);
        hid_device_info* cur = infos;

        hid_device* dev = nullptr;
        while (cur) {
            if ((cur->vendor_id == dse::kSonyVid) &&
                (cur->product_id == dse::kPidDualSense || cur->product_id == dse::kPidDualSenseEdge) &&
                cur->path) {
                dev = hid_open_path(cur->path);
                if (dev) {
                    hid_set_nonblocking(dev, 1);
                    logger::info("[DualPad] DualSense connected: VID={:04X} PID={:04X}",
                        cur->vendor_id, cur->product_id);
                    break;
                }
            }
            cur = cur->next;
        }

        hid_free_enumeration(infos);
        return dev;
    }

    void ReaderLoop()
    {
        logger::info("[DualPad] HID reader thread started");

        if (hid_init() != 0) {
            logger::error("[DualPad] hid_init failed");
            return;
        }

        hid_device* dev = nullptr;
        dse::State prevState{};
        std::uint32_t prevMask = 0;
        dualpad::input::TouchpadGestureRecognizer gesture;

        while (g_running.load()) {
            if (!dev) {
                dev = TryOpenDualSense();
                if (!dev) {
                    std::this_thread::sleep_for(1000ms);
                    continue;
                }
                prevState = {};
                prevMask = 0;
                gesture.Reset();
            }

            unsigned char buf[64]{};
            const int n = hid_read_timeout(dev, buf, sizeof(buf), 8);

            if (n > 0) {
                dse::State state{};
                if (!dse::ParseReport01(buf, n, state)) {
                    continue;
                }

                // 构建按键掩码
                std::uint32_t mask = BuildButtonMask(state);

                // === 触摸板手势 ===
                const auto g = gesture.Update(state);
                if (g != dualpad::input::TouchGesture::None) {
                    const auto gMask = GestureToMask(g);
                    if (gMask != 0) {
                        logger::info("[DualPad] Gesture detected: {}",
                            dualpad::input::ToString(g));

                        // Pulse 手势按键（用于 XInput）
                        dualpad::input::SyntheticPadState::GetSingleton().PulseButton(gMask);

                        // 查询绑定并执行动作
                        dualpad::input::Trigger trigger;
                        trigger.type = dualpad::input::TriggerType::Gesture;
                        trigger.code = gMask;

                        auto context = dualpad::input::ContextManager::GetSingleton().GetCurrentContext();
                        auto actionId = dualpad::input::BindingManager::GetSingleton()
                            .GetActionForTrigger(trigger, context);

                        if (actionId) {
                            dualpad::input::ActionExecutor::GetSingleton().Execute(*actionId, context);
                        }
                    }
                }

                // 更新按键状态
                const std::uint32_t pressed = mask & ~prevMask;
                const std::uint32_t released = prevMask & ~mask;

                if (pressed != 0) {
                    dualpad::input::SyntheticPadState::GetSingleton().SetButton(pressed, true);

                    // 背键日志
                    if (pressed & static_cast<std::uint32_t>(BtnCode::BackLeft)) {
                        logger::info("[DualPad] Back Left pressed");

                        // 查询绑定并执行动作
                        dualpad::input::Trigger trigger;
                        trigger.type = dualpad::input::TriggerType::Button;
                        trigger.code = static_cast<std::uint32_t>(BtnCode::BackLeft);

                        auto context = dualpad::input::ContextManager::GetSingleton().GetCurrentContext();
                        auto actionId = dualpad::input::BindingManager::GetSingleton()
                            .GetActionForTrigger(trigger, context);

                        if (actionId) {
                            dualpad::input::ActionExecutor::GetSingleton().Execute(*actionId, context);
                        }
                    }
                    if (pressed & static_cast<std::uint32_t>(BtnCode::BackRight)) {
                        logger::info("[DualPad] Back Right pressed");

                        // 查询绑定并执行动作
                        dualpad::input::Trigger trigger;
                        trigger.type = dualpad::input::TriggerType::Button;
                        trigger.code = static_cast<std::uint32_t>(BtnCode::BackRight);

                        auto context = dualpad::input::ContextManager::GetSingleton().GetCurrentContext();
                        auto actionId = dualpad::input::BindingManager::GetSingleton()
                            .GetActionForTrigger(trigger, context);

                        if (actionId) {
                            dualpad::input::ActionExecutor::GetSingleton().Execute(*actionId, context);
                        }
                    }
                }
                if (released != 0) {
                    dualpad::input::SyntheticPadState::GetSingleton().SetButton(released, false);
                }

                prevMask = mask;

                // 更新摇杆
                const float lx = NormalizeU8(state.lx);
                const float ly = -NormalizeU8(state.ly);
                const float rx = NormalizeU8(state.rx);
                const float ry = -NormalizeU8(state.ry);
                const float l2 = static_cast<float>(state.l2) / 255.0f;
                const float r2 = static_cast<float>(state.r2) / 255.0f;

                dualpad::input::SyntheticPadState::GetSingleton().SetAxis(lx, ly, rx, ry, l2, r2);

                prevState = state;
            }
            else if (n < 0) {
                logger::warn("[DualPad] HID read error, reconnecting...");
                hid_close(dev);
                dev = nullptr;
                std::this_thread::sleep_for(500ms);
            }
        }

        if (dev) {
            hid_close(dev);
        }

        hid_exit();
        logger::info("[DualPad] HID reader thread stopped");
    }
}

namespace dualpad::input
{
    void StartHidReader()
    {
        if (g_running.exchange(true)) {
            return;
        }

        g_thread = std::thread(ReaderLoop);
        logger::info("[DualPad] HID reader started");
    }

    void StopHidReader()
    {
        if (!g_running.exchange(false)) {
            return;
        }

        if (g_thread.joinable()) {
            g_thread.join();
        }

        logger::info("[DualPad] HID reader stopped");
    }
}