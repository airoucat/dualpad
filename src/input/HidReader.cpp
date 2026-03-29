#include "pch.h"
#include "input/HidReader.h"

#include "input/BindingConfig.h"
#include "input/hid/DualSenseDevice.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/PadEventSnapshot.h"
#include "input/InputContext.h"
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

    void ReaderLoop()
    {
        logger::info("[DualPad] HID reader thread started");

        if (!dualpad::input::HidTransport::InitializeApi()) {
            return;
        }

        dualpad::input::DualSenseDevice device;
        dualpad::input::PadState previousState{};
        dualpad::input::PadEventGenerator eventGenerator;
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
                dualpad::input::PadEventSnapshotDispatcher::GetSingleton().SubmitReset();
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
                    dualpad::input::PadEventSnapshotDispatcher::GetSingleton().SubmitReset();
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

            const auto& contextManager = dualpad::input::ContextManager::GetSingleton();
            const auto snapshotContext = contextManager.GetCurrentContext();
            const auto snapshotContextEpoch = contextManager.GetCurrentEpoch();

            dualpad::input::PadEventBuffer events{};
            eventGenerator.Generate(previousState, currentState, snapshotContext, events);

            dualpad::input::PadEventSnapshot snapshot{};
            snapshot.type = dualpad::input::PadEventSnapshotType::Input;
            snapshot.firstSequence = currentState.sequence;
            snapshot.sequence = currentState.sequence;
            snapshot.sourceTimestampUs = currentState.timestampUs;
            snapshot.context = snapshotContext;
            snapshot.contextEpoch = snapshotContextEpoch;
            snapshot.state = currentState;
            snapshot.events = events;
            snapshot.overflowed = events.overflowed;
            dualpad::input::PadEventSnapshotDispatcher::GetSingleton().SubmitSnapshot(snapshot);

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
