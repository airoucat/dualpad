#include "pch.h"

#include "input_v2/runtime/RuntimeOwnerGuard.h"

#include <functional>
#include <utility>

namespace logger = SKSE::log;

namespace dualpad::input_v2::runtime
{
    RuntimeOwnerTick::RuntimeOwnerTick(
        RuntimeOwnerGuard* guard,
        std::uint64_t generation,
        RuntimeOwnerFailure failure) noexcept :
        _guard(guard),
        _generation(generation),
        _failure(failure)
    {}

    RuntimeOwnerTick::RuntimeOwnerTick(RuntimeOwnerTick&& other) noexcept :
        _guard(std::exchange(other._guard, nullptr)),
        _generation(std::exchange(other._generation, 0)),
        _failure(std::exchange(other._failure, RuntimeOwnerFailure::None))
    {}

    RuntimeOwnerTick& RuntimeOwnerTick::operator=(RuntimeOwnerTick&& other) noexcept
    {
        if (this != &other) {
            Release();
            _guard = std::exchange(other._guard, nullptr);
            _generation = std::exchange(other._generation, 0);
            _failure = std::exchange(other._failure, RuntimeOwnerFailure::None);
        }
        return *this;
    }

    RuntimeOwnerTick::~RuntimeOwnerTick()
    {
        Release();
    }

    void RuntimeOwnerTick::Release() noexcept
    {
        if (auto* guard = std::exchange(_guard, nullptr); guard) {
            guard->Exit(_generation);
        }
    }

    RuntimeOwnerGuard& RuntimeOwnerGuard::GetSingleton()
    {
        static RuntimeOwnerGuard guard;
        return guard;
    }

    RuntimeOwnerTick RuntimeOwnerGuard::TryEnter(std::uint64_t frameToken)
    {
        std::scoped_lock lock(_mutex);
        if (_snapshot.degraded) {
            ++_snapshot.rejectedTicks;
            return RuntimeOwnerTick(nullptr, _snapshot.generation, RuntimeOwnerFailure::AlreadyDegraded);
        }
        if (frameToken == 0) {
            return RejectLocked(RuntimeOwnerFailure::InvalidFrameToken, frameToken);
        }

        const auto currentThread = std::this_thread::get_id();
        if (!_snapshot.ownerBound) {
            _ownerThread = currentThread;
            _snapshot.ownerBound = true;
            _snapshot.ownerThreadHash = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(currentThread));
            logger::info(
                "[DualPad][RuntimeOwner] event=bound threadHash={} frameToken={}",
                _snapshot.ownerThreadHash,
                frameToken);
        } else if (_ownerThread != currentThread) {
            return RejectLocked(RuntimeOwnerFailure::ThreadDrift, frameToken);
        }

        if (_snapshot.tickActive) {
            return RejectLocked(RuntimeOwnerFailure::ReentrantTick, frameToken);
        }
        if (_snapshot.lastFrameToken != 0 && frameToken == _snapshot.lastFrameToken) {
            return RejectLocked(RuntimeOwnerFailure::RepeatedFrameToken, frameToken);
        }
        if (_snapshot.lastFrameToken != 0 && frameToken < _snapshot.lastFrameToken) {
            return RejectLocked(RuntimeOwnerFailure::NonMonotonicFrameToken, frameToken);
        }

        _snapshot.tickActive = true;
        _snapshot.lastFrameToken = frameToken;
        _snapshot.failure = RuntimeOwnerFailure::None;
        ++_snapshot.generation;
        if (_snapshot.generation == 1 || (_snapshot.generation % 600) == 0) {
            logger::info(
                "[DualPad][RuntimeOwner] event=tick generation={} frameToken={} threadHash={}",
                _snapshot.generation,
                frameToken,
                _snapshot.ownerThreadHash);
        }
        return RuntimeOwnerTick(this, _snapshot.generation, RuntimeOwnerFailure::None);
    }

    RuntimeOwnerTick RuntimeOwnerGuard::RejectLocked(
        RuntimeOwnerFailure failure,
        std::uint64_t frameToken)
    {
        _snapshot.degraded = true;
        _snapshot.failure = failure;
        _publishedFailure.store(failure, std::memory_order_release);
        ++_snapshot.rejectedTicks;
        if (!_loggedDegraded) {
            logger::critical(
                "[DualPad][RuntimeOwner] event=degraded failure={} frameToken={} lastFrameToken={} generation={} ownerThreadHash={} currentThreadHash={}",
                ToString(failure),
                frameToken,
                _snapshot.lastFrameToken,
                _snapshot.generation,
                _snapshot.ownerThreadHash,
                static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
            _loggedDegraded = true;
        }
        return RuntimeOwnerTick(nullptr, _snapshot.generation, failure);
    }

    void RuntimeOwnerGuard::Exit(std::uint64_t generation) noexcept
    {
        std::scoped_lock lock(_mutex);
        if (_snapshot.tickActive && _snapshot.generation == generation) {
            _snapshot.tickActive = false;
        }
    }

    RuntimeOwnerSnapshot RuntimeOwnerGuard::GetSnapshot() const
    {
        std::scoped_lock lock(_mutex);
        return _snapshot;
    }

    RuntimeOwnerFailure RuntimeOwnerGuard::GetPublishedFailure() const noexcept
    {
        return _publishedFailure.load(std::memory_order_acquire);
    }

    bool RuntimeOwnerGuard::IsCurrentThreadOwnerTick() const
    {
        std::scoped_lock lock(_mutex);
        return _snapshot.ownerBound &&
            _snapshot.tickActive &&
            !_snapshot.degraded &&
            _ownerThread == std::this_thread::get_id();
    }

    void RuntimeOwnerGuard::Stop()
    {
        std::scoped_lock lock(_mutex);
        if (!_snapshot.degraded) {
            (void)RejectLocked(RuntimeOwnerFailure::OwnerStopped, _snapshot.lastFrameToken);
        }
    }

    void RuntimeOwnerGuard::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _ownerThread = {};
        _snapshot = RuntimeOwnerSnapshot{};
        _loggedDegraded = false;
        _publishedFailure.store(RuntimeOwnerFailure::None, std::memory_order_release);
    }

    const char* ToString(RuntimeOwnerFailure failure) noexcept
    {
        switch (failure) {
        case RuntimeOwnerFailure::InvalidFrameToken:
            return "invalid_frame_token";
        case RuntimeOwnerFailure::ReentrantTick:
            return "reentrant_tick";
        case RuntimeOwnerFailure::RepeatedFrameToken:
            return "repeated_frame_token";
        case RuntimeOwnerFailure::NonMonotonicFrameToken:
            return "non_monotonic_frame_token";
        case RuntimeOwnerFailure::ThreadDrift:
            return "thread_drift";
        case RuntimeOwnerFailure::OwnerStopped:
            return "owner_stopped";
        case RuntimeOwnerFailure::AlreadyDegraded:
            return "already_degraded";
        case RuntimeOwnerFailure::None:
        default:
            return "none";
        }
    }
}
