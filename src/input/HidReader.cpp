#include "pch.h"
#include "input/HidReader.h"

#include "input/hid/DualSenseDevice.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/protocol/DualSenseProtocol.h"
#include "input/state/PadStateDebugger.h"
#include "input/state/PadStateNormalizer.h"
#include "input_v2/ingress/GamepadActivityClassifier.h"
#include "input_v2/ingress/IngressHub.h"
#include "haptics/HidOutput.h"

#include <SKSE/SKSE.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace logger = SKSE::log;

namespace
{
    std::atomic_bool g_running{ false };
    std::mutex g_threadMutex;
    std::thread* g_thread{ nullptr };

    using namespace std::chrono_literals;

    void ReaderLoop()
    {
        logger::info("[DualPad] HID reader thread started");

        if (!dualpad::input::HidTransport::InitializeApi()) {
            return;
        }

        dualpad::input::DualSenseDevice device;
        dualpad::input_v2::ingress::GamepadActivityClassifier classifier;
        dualpad::input::PadState previousState{};
        bool havePreviousState = false;
        bool connectionPublished = false;

        const auto publishDisconnect = [&]() {
            if (connectionPublished) {
                (void)dualpad::input_v2::ingress::IngressHub::GetSingleton().PublishGamepadDisconnect();
                dualpad::input::PadEventSnapshotDispatcher::GetSingleton().NotifyIngressPublished();
            }
            classifier.Reset(dualpad::input_v2::ingress::ToMask(
                dualpad::input_v2::ingress::InputResetReason::DeviceDisconnected));
            previousState = {};
            havePreviousState = false;
            connectionPublished = false;
        };

        while (g_running.load(std::memory_order_acquire)) {
            if (!device.IsOpen()) {
                if (!device.Open()) {
                    std::this_thread::sleep_for(1000ms);
                    continue;
                }

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
                    publishDisconnect();
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

            const auto& previous = havePreviousState ? previousState : dualpad::input::PadState{};
            auto classified = classifier.Classify(
                previous,
                currentState,
                currentState.sequence,
                currentState.timestampUs);
            const auto connectionChange = connectionPublished ?
                std::optional<dualpad::input_v2::ingress::GamepadConnectionDraft>{} :
                std::optional<dualpad::input_v2::ingress::GamepadConnectionDraft>{
                    dualpad::input_v2::ingress::GamepadConnectionDraft{
                        .connectivity = dualpad::input_v2::ingress::GamepadConnectivity::Connected
                    }
                };
            const auto receipt = dualpad::input_v2::ingress::IngressHub::GetSingleton().PublishGamepadBatch(
                std::move(classified),
                connectionChange);
            if (receipt.accepted && connectionChange) {
                connectionPublished = true;
            }
            previousState = currentState;
            havePreviousState = true;
            dualpad::input::PadEventSnapshotDispatcher::GetSingleton().NotifyIngressPublished();

        }

        publishDisconnect();
        dualpad::haptics::HidOutput::GetSingleton().SetDevice(nullptr);
        device.Close();
        dualpad::input::HidTransport::ShutdownApi();

        logger::info("[DualPad] HID reader thread stopped");
    }
}

namespace dualpad::input
{
    bool IsHidReaderRunning()
    {
        return g_running.load(std::memory_order_acquire);
    }

    void StartHidReader()
    {
        if (g_running.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        {
            std::scoped_lock lock(g_threadMutex);
            delete g_thread;
            g_thread = new std::thread(ReaderLoop);
        }
        logger::info("[DualPad] HID reader started");
    }

    void StopHidReader()
    {
        if (!g_running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        std::thread* thread = nullptr;
        {
            std::scoped_lock lock(g_threadMutex);
            thread = std::exchange(g_thread, nullptr);
        }

        if (thread) {
            if (thread->joinable()) {
                thread->join();
            }
            delete thread;
        }

        logger::info("[DualPad] HID reader stopped");
    }
}
