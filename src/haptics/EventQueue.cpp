#include "pch.h"
#include "haptics/EventQueue.h"

#include <SKSE/SKSE.h>
#include <chrono>
#include <thread>

namespace logger = SKSE::log;

namespace
{
    inline std::uint64_t SteadyNowUs()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    }

    inline std::uint64_t CurrentThreadHash64()
    {
        return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
}

namespace dualpad::haptics
{
    EventQueue& EventQueue::GetSingleton()
    {
        static EventQueue instance;
        return instance;
    }

    void EventQueue::Initialize(std::size_t capacity)
    {
        if (capacity == 0) {
            capacity = 1;
        }

        bool expected = false;
        if (!_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            logger::warn("[Haptics][EventQueue] Initialize ignored: already initialized (capacity={})", _capacity.load(std::memory_order_acquire));
            return;
        }

        auto newQueue = std::make_shared<MPSCQueue<EventMsg>>(capacity);
        {
            std::lock_guard<std::mutex> lock(_queuePtrMtx);
            _queue = std::move(newQueue);
        }

        _capacity.store(_queue->Capacity(), std::memory_order_release);

        // 每次初始化都重置统计，便于阶段验收
        ResetStats();
        _consumerTidHash.store(0, std::memory_order_release);
        _lastConsumerWarnUs.store(0, std::memory_order_release);

        logger::info("[Haptics][EventQueue] initialized capacity={}", _capacity.load(std::memory_order_acquire));
    }

    void EventQueue::Shutdown()
    {
        bool expected = true;
        if (!_initialized.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            return;
        }

        std::shared_ptr<MPSCQueue<EventMsg>> oldQueue;
        {
            std::lock_guard<std::mutex> lock(_queuePtrMtx);
            oldQueue.swap(_queue);  // 成员置空；旧对象由 oldQueue 托管到函数末尾安全释放
        }

        const auto pushed = _pushed.load(std::memory_order_relaxed);
        const auto dropped = _dropped.load(std::memory_order_relaxed);
        const auto drained = _drainedEvents.load(std::memory_order_relaxed);
        const auto drainCalls = _drainCalls.load(std::memory_order_relaxed);

        logger::info(
            "[Haptics][EventQueue] shutdown pushed={} dropped={} drained={} drainCalls={} lastSizeApprox={}",
            pushed, dropped, drained, drainCalls, oldQueue ? oldQueue->SizeApprox() : 0);

        _capacity.store(0, std::memory_order_release);
    }

    bool EventQueue::Push(const EventMsg& msg)
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            _dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        std::shared_ptr<MPSCQueue<EventMsg>> q;
        {
            std::lock_guard<std::mutex> lock(_queuePtrMtx);
            q = _queue;
        }
        if (!q) {
            _dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (q->TryPush(msg)) {
            _pushed.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        _dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    std::vector<EventMsg> EventQueue::DrainAll(std::size_t maxDrain)
    {
        std::vector<EventMsg> out;
        if (maxDrain == 0) {
            return out;
        }

        if (!_initialized.load(std::memory_order_acquire)) {
            return out;
        }

        CheckConsumerThread();

        std::shared_ptr<MPSCQueue<EventMsg>> q;
        {
            std::lock_guard<std::mutex> lock(_queuePtrMtx);
            q = _queue;
        }
        if (!q) {
            return out;
        }

        out.reserve(maxDrain);

        EventMsg tmp{};
        while (out.size() < maxDrain && q->TryPop(tmp)) {
            out.push_back(tmp);
        }

        _drainCalls.fetch_add(1, std::memory_order_relaxed);
        _drainedEvents.fetch_add(static_cast<std::uint64_t>(out.size()), std::memory_order_relaxed);

        return out;
    }

    std::size_t EventQueue::SizeApprox() const
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            return 0;
        }

        std::shared_ptr<MPSCQueue<EventMsg>> q;
        {
            std::lock_guard<std::mutex> lock(_queuePtrMtx);
            q = _queue;
        }
        return q ? q->SizeApprox() : 0;
    }

    std::size_t EventQueue::Capacity() const
    {
        return _capacity.load(std::memory_order_acquire);
    }

    void EventQueue::ResetStats()
    {
        _pushed.store(0, std::memory_order_relaxed);
        _dropped.store(0, std::memory_order_relaxed);
        _drainCalls.store(0, std::memory_order_relaxed);
        _drainedEvents.store(0, std::memory_order_relaxed);
    }

    void EventQueue::CheckConsumerThread()
    {
        const auto tid = CurrentThreadHash64();
        std::uint64_t expected = 0;

        // 首次调用绑定消费者线程
        if (_consumerTidHash.compare_exchange_strong(expected, tid, std::memory_order_acq_rel)) {
            logger::info("[Haptics][EventQueue] consumer thread bound tidHash={}", tid);
            return;
        }

        // 后续若线程变更，告警（5秒节流）
        const auto bound = _consumerTidHash.load(std::memory_order_acquire);
        if (bound != tid) {
            const auto nowUs = SteadyNowUs();
            const auto lastUs = _lastConsumerWarnUs.load(std::memory_order_relaxed);

            constexpr std::uint64_t kWarnThrottleUs = 5'000'000ULL; // 5s
            if (nowUs - lastUs >= kWarnThrottleUs) {
                _lastConsumerWarnUs.store(nowUs, std::memory_order_relaxed);
                logger::warn("[Haptics][EventQueue] DrainAll called from different thread! boundTidHash={} currentTidHash={}", bound, tid);
            }
        }
    }
}  // namespace dualpad::haptics