#include "pch.h"
#include "input/HidReader.h"

#include "input/hid/DualSenseDevice.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/PadEventSnapshot.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/LiveInputFactProducer.h"
#include "input/protocol/DualSenseProtocol.h"
#include "input/state/PadStateDebugger.h"
#include "input/state/PadStateNormalizer.h"
#include "haptics/HidOutput.h"

#include <SKSE/SKSE.h>

#include <atomic>
#include <chrono>
#include <mutex>
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
        while (g_running.load(std::memory_order_acquire)) {
            if (!device.IsOpen()) {
                if (!device.Open()) {
                    std::this_thread::sleep_for(1000ms);
                    continue;
                }

                dualpad::input::PadEventSnapshotDispatcher::GetSingleton().SubmitReset();
                dualpad::input_v2::ingress::LiveInputFactProducer::GetSingleton().Reset();
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
                    dualpad::input_v2::ingress::LiveInputFactProducer::GetSingleton().Reset();
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

            const auto& contextSnapshot =
                dualpad::input_v2::context::ContextResolver::GetSingleton().GetPublishedSnapshot();
            const auto snapshotContext = contextSnapshot.legacyInputContext;
            const auto snapshotContextEpoch = contextSnapshot.legacyContextEpoch;
            const auto snapshotContextRevision = contextSnapshot.contextRevision;

            dualpad::input::PadEventBuffer events{};
            dualpad::input_v2::ingress::LiveInputFactProducer::GetSingleton().PublishGamepadSourceEvidence(
                contextSnapshot,
                currentState.timestampUs);

            dualpad::input::PadEventSnapshot snapshot{};
            snapshot.type = dualpad::input::PadEventSnapshotType::Input;
            snapshot.firstSequence = currentState.sequence;
            snapshot.sequence = currentState.sequence;
            snapshot.sourceTimestampUs = currentState.timestampUs;
            snapshot.context = snapshotContext;
            snapshot.contextEpoch = snapshotContextEpoch;
            snapshot.contextRevision = snapshotContextRevision;
            snapshot.state = currentState;
            snapshot.events = events;
            snapshot.overflowed = events.overflowed;
            dualpad::input::PadEventSnapshotDispatcher::GetSingleton().SubmitSnapshot(snapshot);

        }

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
