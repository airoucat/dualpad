#pragma once

#include "haptics/HapticsTypes.h"
#include "haptics/MPSCQueue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace dualpad::haptics
{
    // MPSC：多生产者（事件源线程）-> 单消费者（haptics worker/scorer）
    class EventQueue
    {
    public:
        static EventQueue& GetSingleton();

        // 初始化：可重复调用，已初始化时会告警并返回
        void Initialize(std::size_t capacity = 1024);
        void Shutdown();

        // 生产者调用
        bool Push(const EventMsg& msg);

        // 消费者调用（建议仅一个线程）
        std::vector<EventMsg> DrainAll(std::size_t maxDrain = 256);

        std::size_t SizeApprox() const;
        std::size_t Capacity() const;

        // 统计
        std::uint64_t GetPushedCount() const { return _pushed.load(std::memory_order_relaxed); }
        std::uint64_t GetDroppedCount() const { return _dropped.load(std::memory_order_relaxed); }
        std::uint64_t GetDrainCalls() const { return _drainCalls.load(std::memory_order_relaxed); }
        std::uint64_t GetDrainedEvents() const { return _drainedEvents.load(std::memory_order_relaxed); }

        void ResetStats();

    private:
        EventQueue() = default;
        EventQueue(const EventQueue&) = delete;
        EventQueue& operator=(const EventQueue&) = delete;

        // 单消费者线程校验（MPSC 语义防护）
        void CheckConsumerThread();

        // _queue 指针生命周期保护：
        // - 成员用 shared_ptr
        // - 调用侧先在短锁内拷贝本地 shared_ptr，再无锁使用
        mutable std::mutex _queuePtrMtx;
        std::shared_ptr<MPSCQueue<EventMsg>> _queue;

        std::atomic<bool> _initialized{ false };
        std::atomic<std::size_t> _capacity{ 0 };

        std::atomic<std::uint64_t> _pushed{ 0 };
        std::atomic<std::uint64_t> _dropped{ 0 };
        std::atomic<std::uint64_t> _drainCalls{ 0 };
        std::atomic<std::uint64_t> _drainedEvents{ 0 };

        // 首次 DrainAll 线程哈希，后续不一致会告警
        std::atomic<std::uint64_t> _consumerTidHash{ 0 };

        // 跨线程消费告警节流（微秒时间戳）
        std::atomic<std::uint64_t> _lastConsumerWarnUs{ 0 };
    };
}  // namespace dualpad::haptics