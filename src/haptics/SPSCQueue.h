#pragma once
#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <cassert>

namespace dualpad::haptics
{
    // 固定容量 SPSC 无锁环形队列
    // T 必须可默认构造、可移动
    template <typename T>
    class SPSCQueue
    {
        static_assert(std::is_default_constructible_v<T>);
        static_assert(std::is_move_assignable_v<T>);

    public:
        explicit SPSCQueue(std::size_t capacity)
            : _capacity(capacity + 1)  // 留一个空位区分满/空
            , _buffer(new T[_capacity])
        {
            assert(capacity > 0);
        }

        ~SPSCQueue()
        {
            delete[] _buffer;
        }

        SPSCQueue(const SPSCQueue&) = delete;
        SPSCQueue& operator=(const SPSCQueue&) = delete;

        // 生产者调用（音频线程）
        bool TryPush(T&& item)
        {
            const auto head = _head.load(std::memory_order_relaxed);
            const auto nextHead = Advance(head);

            if (nextHead == _tail.load(std::memory_order_acquire)) {
                return false;  // 队列满
            }

            _buffer[head] = std::move(item);
            _head.store(nextHead, std::memory_order_release);
            return true;
        }

        bool TryPush(const T& item)
        {
            T copy = item;
            return TryPush(std::move(copy));
        }

        // 消费者调用（触觉线程）
        bool TryPop(T& item)
        {
            const auto tail = _tail.load(std::memory_order_relaxed);

            if (tail == _head.load(std::memory_order_acquire)) {
                return false;  // 队列空
            }

            item = std::move(_buffer[tail]);
            _tail.store(Advance(tail), std::memory_order_release);
            return true;
        }

        // 批量弹出（消费者侧）
        std::size_t PopBatch(T* out, std::size_t maxCount)
        {
            std::size_t count = 0;

            while (count < maxCount) {
                if (!TryPop(out[count])) {
                    break;
                }
                ++count;
            }

            return count;
        }

        std::size_t SizeApprox() const
        {
            const auto head = _head.load(std::memory_order_acquire);
            const auto tail = _tail.load(std::memory_order_acquire);

            if (head >= tail) {
                return head - tail;
            }
            return _capacity - (tail - head);
        }

        bool Empty() const
        {
            return _head.load(std::memory_order_acquire)
                == _tail.load(std::memory_order_acquire);
        }

        std::size_t Capacity() const { return _capacity - 1; }

    private:
        std::size_t Advance(std::size_t idx) const
        {
            return (idx + 1 < _capacity) ? (idx + 1) : 0;
        }

        const std::size_t _capacity;
        T* _buffer;

        // 分开 cacheline 避免 false sharing
        alignas(64) std::atomic<std::size_t> _head{ 0 };
        alignas(64) std::atomic<std::size_t> _tail{ 0 };
    };
}