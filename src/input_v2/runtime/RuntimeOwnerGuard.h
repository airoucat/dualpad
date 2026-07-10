#pragma once

#include <cstdint>
#include <mutex>
#include <thread>

namespace dualpad::input_v2::runtime
{
    enum class RuntimeOwnerFailure : std::uint8_t
    {
        None = 0,
        InvalidFrameToken,
        ReentrantTick,
        RepeatedFrameToken,
        NonMonotonicFrameToken,
        ThreadDrift,
        OwnerStopped,
        AlreadyDegraded
    };

    struct RuntimeOwnerSnapshot
    {
        bool ownerBound{ false };
        bool tickActive{ false };
        bool degraded{ false };
        std::uint64_t generation{ 0 };
        std::uint64_t lastFrameToken{ 0 };
        std::uint64_t ownerThreadHash{ 0 };
        std::uint64_t rejectedTicks{ 0 };
        RuntimeOwnerFailure failure{ RuntimeOwnerFailure::None };
    };

    class RuntimeOwnerGuard;

    class RuntimeOwnerTick
    {
    public:
        RuntimeOwnerTick() = default;
        RuntimeOwnerTick(const RuntimeOwnerTick&) = delete;
        RuntimeOwnerTick& operator=(const RuntimeOwnerTick&) = delete;
        RuntimeOwnerTick(RuntimeOwnerTick&& other) noexcept;
        RuntimeOwnerTick& operator=(RuntimeOwnerTick&& other) noexcept;
        ~RuntimeOwnerTick();

        [[nodiscard]] bool Accepted() const noexcept { return _guard != nullptr; }
        [[nodiscard]] std::uint64_t Generation() const noexcept { return _generation; }
        [[nodiscard]] RuntimeOwnerFailure Failure() const noexcept { return _failure; }

    private:
        friend class RuntimeOwnerGuard;
        RuntimeOwnerTick(
            RuntimeOwnerGuard* guard,
            std::uint64_t generation,
            RuntimeOwnerFailure failure) noexcept;
        void Release() noexcept;

        RuntimeOwnerGuard* _guard{ nullptr };
        std::uint64_t _generation{ 0 };
        RuntimeOwnerFailure _failure{ RuntimeOwnerFailure::None };
    };

    class RuntimeOwnerGuard
    {
    public:
        static RuntimeOwnerGuard& GetSingleton();

        RuntimeOwnerTick TryEnter(std::uint64_t frameToken);
        [[nodiscard]] RuntimeOwnerSnapshot GetSnapshot() const;
        [[nodiscard]] bool IsCurrentThreadOwnerTick() const;
        void Stop();
        void ResetForTests();

    private:
        friend class RuntimeOwnerTick;
        void Exit(std::uint64_t generation) noexcept;
        RuntimeOwnerTick RejectLocked(RuntimeOwnerFailure failure, std::uint64_t frameToken);

        mutable std::mutex _mutex;
        std::thread::id _ownerThread{};
        RuntimeOwnerSnapshot _snapshot{};
        bool _loggedDegraded{ false };
    };

    const char* ToString(RuntimeOwnerFailure failure) noexcept;
}
