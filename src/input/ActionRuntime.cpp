#include "pch.h"
#include "input/ActionRuntime.h"
#include "input/ActionRouter.h"
#include "input/ActionConfig.h"
#include "input/GameActionOutput.h"
#include "input/InputIngress.h"
#include "input/NativeUserEventBridge.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <SKSE/SKSE.h>
namespace logger = SKSE::log;

namespace
{
    std::atomic_bool g_tickRun{ false };
    std::atomic_bool g_tickTaskQueued{ false };
    std::jthread g_tickScheduler;

    void QueueOneTickTask()
    {
        auto* ti = SKSE::GetTaskInterface();
        if (!ti) {
            return;
        }

        if (g_tickTaskQueued.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        // 用 UI 任务，避免 worker 线程时序问题
        ti->AddUITask([]() {
            if (!g_tickRun.load(std::memory_order_acquire)) {
                g_tickTaskQueued.store(false, std::memory_order_release);
                return;
            }

            dualpad::input::TickActionRuntimeMainThread();
            g_tickTaskQueued.store(false, std::memory_order_release);
            });
    }
}

namespace dualpad::input
{
    void InitActionRuntime()
    {
        ActionRouter::GetSingleton().InitDefaultBindings();
        InitActionConfigHotReload();
        BindGameOutputThread();
        InputIngress::GetSingleton().Reset();
    }

    void TickActionRuntimeMainThread()
    {
        PollActionConfigHotReload();

        static thread_local std::vector<TriggerEvent> evs;
        InputIngress::GetSingleton().DrainTriggers(evs);
        for (const auto& e : evs) {
            ActionRouter::GetSingleton().EmitInput(e.code, e.phase);
        }

        AnalogSample s{};
        if (InputIngress::GetSingleton().ReadLatestAnalog(s)) {
            auto& r = ActionRouter::GetSingleton();
            r.EmitAxis(AxisCode::LStickX, s.lx);
            r.EmitAxis(AxisCode::LStickY, s.ly);
            r.EmitAxis(AxisCode::RStickX, s.rx);
            r.EmitAxis(AxisCode::RStickY, s.ry);
            r.EmitAxis(AxisCode::L2, s.l2);
            r.EmitAxis(AxisCode::R2, s.r2);
        }

        // CollectGameOutputCommands();
        // FlushGameOutputOnBoundThread();

        // NativeUserEventBridge::GetSingleton().FlushQueued();
    }

    void StartActionRuntimeTickOnMainThread()
    {
        if (g_tickRun.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        logger::info("[DualPad] Start runtime tick loop");

        g_tickScheduler = std::jthread([](std::stop_token st) {
            using namespace std::chrono_literals;
            while (!st.stop_requested() && g_tickRun.load(std::memory_order_acquire)) {
                QueueOneTickTask();
                std::this_thread::sleep_for(8ms);
            }
            });
    }

    void StopActionRuntimeTickOnMainThread()
    {
        g_tickRun.store(false, std::memory_order_release);
        if (g_tickScheduler.joinable()) {
            g_tickScheduler.request_stop();
            g_tickScheduler.join();
        }
    }
}