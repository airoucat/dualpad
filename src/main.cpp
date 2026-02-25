#include "pch.h"
#include "input/HidReader.h"
#include "input/ActionRuntime.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace logger = SKSE::log;
using namespace std::chrono_literals;

namespace
{
    std::atomic_bool g_tickRunning{ false };
    std::atomic_bool g_tickTaskQueued{ false };
    std::atomic<uint64_t> g_lastTickMs{ 0 };
    std::jthread g_tickScheduler;
    std::atomic_bool g_stallState{ false };        // 当前是否处于 stalled
    std::atomic<uint64_t> g_lastRecoverMs{ 0 };    // 上次恢复尝试时间

    uint64_t NowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void QueueOneMainThreadTick()
    {
        auto* task = SKSE::GetTaskInterface();
        if (!task) {
            return;
        }

        // 防止同一时刻排入多个 Tick 任务
        if (g_tickTaskQueued.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        task->AddTask([]() {
            try {
                dualpad::input::TickActionRuntimeMainThread();
                g_lastTickMs.store(NowMs(), std::memory_order_release);
            }
            catch (const std::exception& e) {
                logger::critical("[DualPad] TickActionRuntimeMainThread exception: {}", e.what());
            }
            catch (...) {
                logger::critical("[DualPad] TickActionRuntimeMainThread unknown exception");
            }

            g_tickTaskQueued.store(false, std::memory_order_release);
            });
    }

    void StartRuntimeTickLoop()
    {
        if (g_tickRunning.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        g_lastTickMs.store(NowMs(), std::memory_order_release);
        g_stallState.store(false, std::memory_order_release);
        g_lastRecoverMs.store(0, std::memory_order_release);

        logger::info("[DualPad] Start runtime tick loop (scheduler)");

        constexpr uint64_t stallThresholdMs = 1000;   // 1s 无心跳判定 stalled
        constexpr uint64_t recoverCooldownMs = 150;   // stalled 期间每 150ms 重试一次

        g_tickScheduler = std::jthread([](std::stop_token st) {
            while (!st.stop_requested() && g_tickRunning.load(std::memory_order_acquire)) {
                // 正常情况下持续尝试投递（有 g_tickTaskQueued 防重入，不会堆爆）
                QueueOneMainThreadTick();

                const auto now = NowMs();
                const auto last = g_lastTickMs.load(std::memory_order_acquire);
                const bool stalled = (now > last + stallThresholdMs);

                if (stalled) {
                    const bool wasStalled = g_stallState.exchange(true, std::memory_order_acq_rel);

                    // 只在进入 stalled 时打一次
                    if (!wasStalled) {
                        logger::warn("[DualPad] Tick heartbeat stalled (>{}ms)", stallThresholdMs);

                        // 进入 stalled 立即恢复一次（不等 cooldown）
                        g_lastRecoverMs.store(now, std::memory_order_release);
                        g_tickTaskQueued.store(false, std::memory_order_release);
                        QueueOneMainThreadTick();
                    }
                    else {
                        auto expected = g_lastRecoverMs.load(std::memory_order_acquire);
                        if (now > expected + recoverCooldownMs &&
                            g_lastRecoverMs.compare_exchange_strong(expected, now, std::memory_order_acq_rel)) {
                            g_tickTaskQueued.store(false, std::memory_order_release);
                            QueueOneMainThreadTick();
                        }
                    }
                }
                else {
                    const bool wasStalled = g_stallState.exchange(false, std::memory_order_acq_rel);
                    if (wasStalled) {
                        logger::info("[DualPad] Tick heartbeat recovered");
                    }
                }

                std::this_thread::sleep_for(8ms); // ~125Hz
            }
            });
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)
    {
        if (!msg) {
            return;
        }

        switch (msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("[DualPad] SKSE message: DataLoaded -> InitActionRuntime");
            dualpad::input::InitActionRuntime();
            StartRuntimeTickLoop();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    logger::info("DualPad loaded");

    if (auto* messaging = SKSE::GetMessagingInterface(); messaging) {
        if (!messaging->RegisterListener(OnSKSEMessage)) {
            logger::warn("Failed to register SKSE messaging listener");
        }
    }

    dualpad::input::StartHidReader();
    return true;
}