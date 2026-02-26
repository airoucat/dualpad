#include "pch.h"
#include "input/ActionRuntime.h"
#include "input/ActionRouter.h"
#include "input/ActionConfig.h"
#include "input/GameActionOutput.h"
#include "input/AnalogState.h"
#include "input/InputActions.h"
#include "input/AxisDiag.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
namespace logger = SKSE::log;

namespace
{
    std::atomic_bool g_tickRun{ false };
    std::atomic_bool g_tickTaskQueued{ false };
    std::jthread g_tickScheduler;
    // std::atomic_bool g_bound{ false };

    void QueueOneTickTask()
    {
        auto* ti = SKSE::GetTaskInterface();
        if (!ti) {
            return;
        }

        if (g_tickTaskQueued.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        ti->AddTask([]() {
            if (!g_tickRun.load(std::memory_order_acquire)) {
                g_tickTaskQueued.store(false, std::memory_order_release);
                return;
            }
            // dualpad::input::BindGameOutputThread();   // 每次都绑当前执行线程
            dualpad::input::TickActionRuntimeMainThread();
            g_tickTaskQueued.store(false, std::memory_order_release);
            });
    }
} // <- 这里一定要关掉匿名 namespace

namespace dualpad::input
{
    void InitActionRuntime()
    {
        ActionRouter::GetSingleton().InitDefaultBindings();
        InitActionConfigHotReload();
    }

    void TickActionRuntimeMainThread()
    {
        PollActionConfigHotReload();

        auto s = AnalogState::GetSingleton().Read();
        auto& r = ActionRouter::GetSingleton();
        r.EmitAxis(AxisCode::LStickX, s.lx);
        r.EmitAxis(AxisCode::LStickY, s.ly);
        r.EmitAxis(AxisCode::RStickX, s.rx);
        r.EmitAxis(AxisCode::RStickY, s.ry);
        r.EmitAxis(AxisCode::L2, s.l2);
        r.EmitAxis(AxisCode::R2, s.r2);

        // CollectGameOutputCommands();
        // FlushGameOutputOnBoundThread();
    }

    void StartActionRuntimeTickOnMainThread()
    {
        if (g_tickRun.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        // g_bound.store(false, std::memory_order_release);
        logger::info("[DualPad] Start runtime tick loop (SKSE task scheduler)");

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