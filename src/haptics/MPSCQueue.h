#pragma once
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace dualpad::haptics
{
    // Bounded MPSC queue (Vyukov style), single consumer.
    // T 建议是可拷贝/可移动的轻量 POD（本工程消息结构满足）
    template <class T, std::size_t Capacity = 4096>
    class MPSCQueue
    {
    public:
        static_assert(Capacity >= 2, "Capacity must be >= 2");
        explicit MPSCQueue(std::size_t capacity)
        {
            _capacity = NextPow2(capacity < 2 ? 2 : capacity);
            _mask = _capacity - 1;
            _buffer = std::make_unique<Cell[]>(_capacity);

            for (std::size_t i = 0; i < _capacity; ++i) {
                _buffer[i].seq.store(i, std::memory_order_relaxed);
            }
            _enqueuePos.store(0, std::memory_order_relaxed);
            _dequeuePos.store(0, std::memory_order_relaxed);
        }

        MPSCQueue(const MPSCQueue&) = delete;
        MPSCQueue& operator=(const MPSCQueue&) = delete;

        bool TryPush(const T& value) { return EnqueueImpl(value); }
        bool TryPush(T&& value) { return EnqueueImpl(std::move(value)); }

        bool TryPop(T& out)
        {
            std::size_t pos = _dequeuePos.load(std::memory_order_relaxed);

            for (;;) {
                Cell& cell = _buffer[pos & _mask];
                const std::size_t seq = cell.seq.load(std::memory_order_acquire);
                const std::intptr_t dif = static_cast<std::intptr_t>(seq) -
                    static_cast<std::intptr_t>(pos + 1);

                if (dif == 0) {
                    if (_dequeuePos.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        out = std::move(cell.data);
                        cell.seq.store(pos + _capacity, std::memory_order_release);
                        return true;
                    }
                }
                else if (dif < 0) {
                    return false;  // empty
                }
                else {
                    pos = _dequeuePos.load(std::memory_order_relaxed);
                }
            }
        }

        std::size_t PopBatch(T* out, std::size_t maxCount)
        {
            if (!out || maxCount == 0) {
                return 0;
            }

            std::size_t n = 0;
            while (n < maxCount) {
                if (!TryPop(out[n])) {
                    break;
                }
                ++n;
            }
            return n;
        }

        std::size_t Capacity() const { return _capacity; }

        std::size_t SizeApprox() const
        {
            const auto enq = _enqueuePos.load(std::memory_order_acquire);
            const auto deq = _dequeuePos.load(std::memory_order_acquire);
            return enq >= deq ? (enq - deq) : 0;
        }

    private:
        struct Cell
        {
            std::atomic<std::size_t> seq{ 0 };
            T data{};
        };

        template <class U>
        bool EnqueueImpl(U&& value)
        {
            std::size_t pos = _enqueuePos.load(std::memory_order_relaxed);

            for (;;) {
                Cell& cell = _buffer[pos & _mask];
                const std::size_t seq = cell.seq.load(std::memory_order_acquire);
                const std::intptr_t dif = static_cast<std::intptr_t>(seq) -
                    static_cast<std::intptr_t>(pos);

                if (dif == 0) {
                    if (_enqueuePos.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        cell.data = std::forward<U>(value);
                        cell.seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (dif < 0) {
                    return false;  // full
                }
                else {
                    pos = _enqueuePos.load(std::memory_order_relaxed);
                }
            }
        }

        static std::size_t NextPow2(std::size_t v)
        {
            if (v <= 2) {
                return 2;
            }
            --v;
            v |= v >> 1;
            v |= v >> 2;
            v |= v >> 4;
            v |= v >> 8;
            v |= v >> 16;
            if constexpr (sizeof(std::size_t) == 8) {
                v |= v >> 32;
            }
            return v + 1;
        }

        std::size_t _capacity{ 0 };
        std::size_t _mask{ 0 };
        std::unique_ptr<Cell[]> _buffer;

        alignas(64) std::atomic<std::size_t> _enqueuePos{ 0 };
        alignas(64) std::atomic<std::size_t> _dequeuePos{ 0 };
    };
}