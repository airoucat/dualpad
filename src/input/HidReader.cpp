#include "pch.h"
#include "input/HidReader.h"

#include "input/BindingManager.h"
#include "input/ActionExecutor.h"
#include "input/BindingConfig.h"
#include "input/InputContext.h"
#include "input/SyntheticPadState.h"
#include "input/hid/DualSenseDevice.h"
#include "input/legacy/InputCompatBridge.h"
#include "input/mapping/BindingResolver.h"
#include "input/mapping/PadEvent.h"
#include "input/mapping/PadEventGenerator.h"
#include "input/protocol/DualSenseProtocol.h"
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

    std::uint32_t DispatchBoundEvent(
        const dualpad::input::PadEvent& event,
        const dualpad::input::BindingResolver& bindingResolver,
        dualpad::input::InputContext context)
    {
        const auto resolved = bindingResolver.Resolve(event, context);
        if (!resolved) {
            return 0;
        }

        logger::info("[DualPad][Mapping] Event {} mapped to action {}",
            dualpad::input::ToString(event.type),
            resolved->actionId);
        dualpad::input::ActionExecutor::GetSingleton().Execute(resolved->actionId, context);

        if (event.type == dualpad::input::PadEventType::ButtonPress) {
            return event.code;
        }

        return 0;
    }

    std::uint32_t DispatchPadEvents(
        const dualpad::input::PadEventBuffer& events,
        const dualpad::input::BindingResolver& bindingResolver)
    {
        std::uint32_t handledButtons = 0;
        if (events.count == 0) {
            return handledButtons;
        }

        const auto context = dualpad::input::ContextManager::GetSingleton().GetCurrentContext();
        for (std::size_t i = 0; i < events.count; ++i) {
            const auto& event = events[i];

            if ((event.type == dualpad::input::PadEventType::Gesture ||
                 event.type == dualpad::input::PadEventType::TouchpadPress ||
                 event.type == dualpad::input::PadEventType::TouchpadSlide) &&
                dualpad::input::IsSyntheticPadBitCode(event.code)) {
                dualpad::input::SyntheticPadState::GetSingleton().PulseButton(event.code);
            }

            handledButtons |= DispatchBoundEvent(event, bindingResolver, context);
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
        dualpad::input::PadEventGenerator eventGenerator;
        dualpad::input::BindingResolver bindingResolver;
        eventGenerator.GetTouchpadMapper().SetConfig(
            dualpad::input::BindingConfig::GetSingleton().GetTouchpadConfig());

        while (g_running.load(std::memory_order_acquire)) {
            if (!device.IsOpen()) {
                if (!device.Open()) {
                    std::this_thread::sleep_for(1000ms);
                    continue;
                }

                previousState = {};
                eventGenerator.Reset();
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
            dualpad::input::PadEventBuffer events{};
            eventGenerator.Generate(previousState, currentState, events);

            const auto handledButtons = DispatchPadEvents(events, bindingResolver);

            const auto previousMask = previousState.buttons.digitalMask;
            const auto currentMask = compatFrame.buttonMask;
            const auto pressedMask = currentMask & ~previousMask;
            const auto releasedMask = previousMask & ~currentMask;
            const auto filteredPressed = pressedMask & ~handledButtons;
            const auto filteredReleased = releasedMask & ~handledButtons;

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
