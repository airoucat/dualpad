#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dualpad::input::backend
{
    enum class KeyboardBridgeCommandType : std::uint8_t
    {
        None = 0,
        Press = 1,
        Release = 2,
        Pulse = 3,
        Reset = 4
    };

    struct KeyboardBridgeCommand
    {
        KeyboardBridgeCommandType type{ KeyboardBridgeCommandType::None };
        std::uint8_t scancode{ 0 };
        std::uint16_t reserved{ 0 };
        std::uint32_t sequence{ 0 };
        std::uint64_t timestampMs{ 0 };
    };

    class KeyboardNativeBridge
    {
    public:
        static KeyboardNativeBridge& GetSingleton();

        bool EnqueuePress(std::uint8_t scancode);
        bool EnqueueRelease(std::uint8_t scancode);
        bool EnqueuePulse(std::uint8_t scancode);
        bool EnqueueReset();

        std::size_t ConsumeCommands(KeyboardBridgeCommand* outCommands, std::size_t capacity);

        void TouchConsumerHeartbeat();
        bool HasConsumerHeartbeat(std::uint64_t maxAgeMs = 1500) const;
        bool HasRecentProducerHeartbeat(std::uint64_t maxAgeMs = 150) const;
        std::uint32_t GetDroppedCount() const;

    private:
        struct SharedState;

        KeyboardNativeBridge() = default;

        bool EnsureInitialized() const;
        bool EnqueueCommand(KeyboardBridgeCommandType type, std::uint8_t scancode);
        bool TryLock(DWORD timeoutMs) const;
        void Unlock() const;
        void ReleaseResources() const;
        std::wstring BuildMappingName() const;
        std::wstring BuildMutexName() const;

        mutable HANDLE _mapping{ nullptr };
        mutable HANDLE _mutex{ nullptr };
        mutable SharedState* _state{ nullptr };
        mutable DWORD _processId{ 0 };
        mutable bool _initialized{ false };
        mutable std::uint32_t _producerSequence{ 0 };
        mutable std::atomic<std::uint64_t> _cachedConsumerHeartbeatCheckMs{ 0 };
        mutable std::atomic_bool _cachedConsumerHeartbeatActive{ false };
    };
}
