#pragma once

#include <cstddef>

namespace dualpad::input::detail
{
    inline constexpr std::ptrdiff_t kSEButtonEventsOffset = 0x20;
    inline constexpr std::ptrdiff_t kSEButtonEventStride = 0x30;
    inline constexpr std::ptrdiff_t kSEThumbstickEventsOffset = 0x2D0;
    inline constexpr std::ptrdiff_t kSEQueueHeadOffset = 0x380;
    inline constexpr std::ptrdiff_t kSEQueueTailOffset = 0x388;

    inline std::byte* GetSEQueueBytes(RE::BSInputEventQueue* queue) noexcept
    {
        return reinterpret_cast<std::byte*>(queue);
    }

    inline const std::byte* GetSEQueueBytes(const RE::BSInputEventQueue* queue) noexcept
    {
        return reinterpret_cast<const std::byte*>(queue);
    }

    inline RE::ButtonEvent* GetSEButtonEventCache(RE::BSInputEventQueue* queue) noexcept
    {
        return reinterpret_cast<RE::ButtonEvent*>(GetSEQueueBytes(queue) + kSEButtonEventsOffset);
    }

    inline RE::ButtonEvent* GetSEButtonEventSlot(RE::BSInputEventQueue* queue, std::size_t index) noexcept
    {
        return reinterpret_cast<RE::ButtonEvent*>(
            GetSEQueueBytes(queue) + kSEButtonEventsOffset + static_cast<std::ptrdiff_t>(index) * kSEButtonEventStride);
    }

    inline RE::InputEvent*& GetSEQueueHead(RE::BSInputEventQueue* queue) noexcept
    {
        return *reinterpret_cast<RE::InputEvent**>(GetSEQueueBytes(queue) + kSEQueueHeadOffset);
    }

    inline RE::InputEvent*& GetSEQueueTail(RE::BSInputEventQueue* queue) noexcept
    {
        return *reinterpret_cast<RE::InputEvent**>(GetSEQueueBytes(queue) + kSEQueueTailOffset);
    }

    inline const RE::InputEvent* GetSEQueueHead(const RE::BSInputEventQueue* queue) noexcept
    {
        return *reinterpret_cast<RE::InputEvent* const*>(GetSEQueueBytes(queue) + kSEQueueHeadOffset);
    }

    inline const RE::InputEvent* GetSEQueueTail(const RE::BSInputEventQueue* queue) noexcept
    {
        return *reinterpret_cast<RE::InputEvent* const*>(GetSEQueueBytes(queue) + kSEQueueTailOffset);
    }
}
