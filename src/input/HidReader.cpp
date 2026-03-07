#include "pch.h"
#include "input/HidReader.h"

#include "input/BindingManager.h"
#include "input/ActionExecutor.h"
#include "input/InputContext.h"
#include "input/PadProfile.h"
#include "input/SyntheticPadState.h"
#include "input/TouchpadGesture.h"
#include "input/Trigger.h"
#include "input/hid/DualSenseDevice.h"
#include "input/legacy/InputCompatBridge.h"
#include "input/protocol/DualSenseProtocol.h"
#include "input/state/PadSnapshot.h"
#include "input/state/PadStateDebugger.h"
#include "input/state/PadStateNormalizer.h"
#include "haptics/HidOutput.h"

#include <SKSE/SKSE.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace logger = SKSE::log;

namespace
{
    std::atomic_bool g_running{ false };
    std::thread g_thread;

    using namespace std::chrono_literals;

    std::uint32_t GestureToMask(dualpad::input::TouchGesture gesture)
    {
        const auto& bits = dualpad::input::GetPadBits(dualpad::input::GetActivePadProfile());

        using TG = dualpad::input::TouchGesture;
        switch (gesture) {
        case TG::LeftPress:
            return bits.tpLeftPress;
        case TG::MidPress:
            return bits.tpMidPress;
        case TG::RightPress:
            return bits.tpRightPress;
        case TG::SwipeUp:
            return bits.tpSwipeUp;
        case TG::SwipeDown:
            return bits.tpSwipeDown;
        case TG::SwipeLeft:
            return bits.tpSwipeLeft;
        case TG::SwipeRight:
            return bits.tpSwipeRight;
        default:
            return 0;
        }
    }

    void DispatchGestureBinding(dualpad::input::TouchGesture gesture)
    {
        const auto gestureMask = GestureToMask(gesture);
        if (gesture == dualpad::input::TouchGesture::None || gestureMask == 0) {
            return;
        }

        logger::info("[DualPad] Gesture detected: {}", dualpad::input::ToString(gesture));
        dualpad::input::SyntheticPadState::GetSingleton().PulseButton(gestureMask);

        dualpad::input::Trigger trigger;
        trigger.type = dualpad::input::TriggerType::Gesture;
        trigger.code = gestureMask;

        auto context = dualpad::input::ContextManager::GetSingleton().GetCurrentContext();
        auto actionId = dualpad::input::BindingManager::GetSingleton()
            .GetActionForTrigger(trigger, context);

        if (actionId) {
            dualpad::input::ActionExecutor::GetSingleton().Execute(*actionId, context);
        }
    }

    std::uint32_t DispatchButtonBindings(std::uint32_t pressedMask)
    {
        std::uint32_t handledButtons = 0;
        if (pressedMask == 0) {
            return handledButtons;
        }

        auto context = dualpad::input::ContextManager::GetSingleton().GetCurrentContext();
        auto& bindingManager = dualpad::input::BindingManager::GetSingleton();

        for (int i = 0; i < 32; ++i) {
            const std::uint32_t buttonMask = (1u << i);
            if ((pressedMask & buttonMask) == 0) {
                continue;
            }

            dualpad::input::Trigger trigger;
            trigger.type = dualpad::input::TriggerType::Button;
            trigger.code = buttonMask;

            auto actionId = bindingManager.GetActionForTrigger(trigger, context);
            if (!actionId) {
                continue;
            }

            logger::info("[DualPad] Button {:08X} has binding: {}", buttonMask, *actionId);
            dualpad::input::ActionExecutor::GetSingleton().Execute(*actionId, context);
            handledButtons |= buttonMask;
        }

        if (handledButtons != 0) {
            logger::info("[DualPad] Blocked buttons from Skyrim: {:08X}", handledButtons);
        }

        return handledButtons;
    }

    void ReaderLoop()
    {
        logger::info("[DualPad] HID reader thread started");

        if (!dualpad::input::HidTransport::InitializeApi()) {
            return;
        }

        dualpad::input::DualSenseDevice device;
        dualpad::input::PadState previousState{};
        dualpad::input::TouchpadGestureRecognizer gesture;

        while (g_running.load(std::memory_order_acquire)) {
            if (!device.IsOpen()) {
                if (!device.Open()) {
                    std::this_thread::sleep_for(1000ms);
                    continue;
                }

                previousState = {};
                gesture.Reset();
                dualpad::haptics::HidOutput::GetSingleton().SetDevice(device.GetNativeHandle());
            }

            dualpad::input::RawInputPacket packet{};
            if (!device.ReadPacket(packet)) {
                switch (device.GetLastReadStatus()) {
                case dualpad::input::ReadStatus::Timeout:
                    continue;
                case dualpad::input::ReadStatus::Disconnected:
                case dualpad::input::ReadStatus::Error:
                    logger::warn("[DualPad] HID device disconnected, reconnecting...");
                    dualpad::haptics::HidOutput::GetSingleton().SetDevice(nullptr);
                    device.Close();
                    std::this_thread::sleep_for(500ms);
                    continue;
                default:
                    continue;
                }
            }

            dualpad::input::LogPacketSummary(packet);
            dualpad::input::LogPacketHexDump(packet);

            dualpad::input::PadState currentState{};
            if (!dualpad::input::ParseDualSenseInputPacket(packet, currentState)) {
                continue;
            }

            dualpad::input::LogParseSuccess(currentState);
            dualpad::input::NormalizePadState(currentState);
            dualpad::input::LogStateSummary(currentState);

            const auto compatFrame = dualpad::input::BuildCompatFrame(currentState);
            currentState.buttons.digitalMask = compatFrame.buttonMask;

            const auto snapshot = dualpad::input::MakePadSnapshot(previousState, currentState);

            const auto detectedGesture = gesture.Update(currentState);
            if (detectedGesture != dualpad::input::TouchGesture::None) {
                DispatchGestureBinding(detectedGesture);
            }

            const auto handledButtons = DispatchButtonBindings(snapshot.pressedMask);
            const auto filteredPressed = snapshot.pressedMask & ~handledButtons;
            const auto filteredReleased = snapshot.releasedMask & ~handledButtons;

            if (filteredPressed != 0) {
                dualpad::input::SyntheticPadState::GetSingleton().SetButton(filteredPressed, true);
            }

            if (filteredReleased != 0) {
                dualpad::input::SyntheticPadState::GetSingleton().SetButton(filteredReleased, false);
            }

            if (compatFrame.hasAxis) {
                dualpad::input::SyntheticPadState::GetSingleton().SetAxis(
                    compatFrame.lx,
                    compatFrame.ly,
                    compatFrame.rx,
                    compatFrame.ry,
                    compatFrame.l2,
                    compatFrame.r2);
            }

            previousState = currentState;
        }

        dualpad::haptics::HidOutput::GetSingleton().SetDevice(nullptr);
        device.Close();
        dualpad::input::HidTransport::ShutdownApi();

        logger::info("[DualPad] HID reader thread stopped");
    }
}

namespace dualpad::input
{
    void StartHidReader()
    {
        if (g_running.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        g_thread = std::thread(ReaderLoop);
        logger::info("[DualPad] HID reader started");
    }

    void StopHidReader()
    {
        if (!g_running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (g_thread.joinable()) {
            g_thread.join();
        }

        logger::info("[DualPad] HID reader stopped");
    }
}
