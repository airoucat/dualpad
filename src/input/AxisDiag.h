#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace dualpad::diag
{
    inline std::atomic<std::uint64_t> axisInPerSec{ 0 };
    inline std::atomic<std::uint64_t> dispatchPerSec{ 0 };
    inline std::atomic<std::uint64_t> applyPerSec{ 0 };
    inline std::atomic<std::uint64_t> overwriteSuspect{ 0 };

    inline std::atomic<float> lastWriteMoveX{ 0.0f };
    inline std::atomic<float> lastWriteMoveY{ 0.0f };
    inline std::atomic<std::uint64_t> lastWriteMs{ 0 };

    inline std::uint64_t NowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
}