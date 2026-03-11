#include "input/backend/KeyboardNativeBridge.h"

#include <algorithm>
#include <cstring>

namespace dualpad::input::backend
{
    namespace
    {
        constexpr std::uint32_t kBridgeMagic = 0x44504B42;  // DPKB
        constexpr std::uint32_t kBridgeVersion = 1;
        constexpr std::size_t kBridgeCapacity = 128;
        constexpr DWORD kBridgeMutexWaitMs = 8;

        std::uint64_t GetMonotonicMs()
        {
            return ::GetTickCount64();
        }
    }

    struct KeyboardNativeBridge::SharedState
    {
        std::uint32_t magic{ 0 };
        std::uint32_t version{ 0 };
        std::uint32_t ownerProcessId{ 0 };
        std::uint32_t producerProcessId{ 0 };
        std::uint32_t consumerProcessId{ 0 };
        std::uint32_t head{ 0 };
        std::uint32_t tail{ 0 };
        std::uint32_t count{ 0 };
        std::uint32_t droppedCount{ 0 };
        std::uint64_t producerHeartbeatMs{ 0 };
        std::uint64_t consumerHeartbeatMs{ 0 };
        KeyboardBridgeCommand commands[kBridgeCapacity]{};
    };

    KeyboardNativeBridge& KeyboardNativeBridge::GetSingleton()
    {
        static KeyboardNativeBridge instance;
        return instance;
    }

    bool KeyboardNativeBridge::EnqueuePress(std::uint8_t scancode)
    {
        return EnqueueCommand(KeyboardBridgeCommandType::Press, scancode);
    }

    bool KeyboardNativeBridge::EnqueueRelease(std::uint8_t scancode)
    {
        return EnqueueCommand(KeyboardBridgeCommandType::Release, scancode);
    }

    bool KeyboardNativeBridge::EnqueuePulse(std::uint8_t scancode)
    {
        return EnqueueCommand(KeyboardBridgeCommandType::Pulse, scancode);
    }

    bool KeyboardNativeBridge::EnqueueReset()
    {
        return EnqueueCommand(KeyboardBridgeCommandType::Reset, 0);
    }

    std::size_t KeyboardNativeBridge::ConsumeCommands(
        KeyboardBridgeCommand* outCommands,
        std::size_t capacity)
    {
        if (outCommands == nullptr || capacity == 0 || !EnsureInitialized() || !TryLock(kBridgeMutexWaitMs)) {
            return 0;
        }

        const auto unlock = [this]() {
            Unlock();
        };

        _state->consumerProcessId = _processId;
        _state->consumerHeartbeatMs = GetMonotonicMs();

        const auto toConsume = (std::min)(capacity, static_cast<std::size_t>(_state->count));
        for (std::size_t i = 0; i < toConsume; ++i) {
            outCommands[i] = _state->commands[_state->head];
            _state->head = (_state->head + 1) % kBridgeCapacity;
        }
        _state->count -= static_cast<std::uint32_t>(toConsume);

        unlock();
        return toConsume;
    }

    void KeyboardNativeBridge::TouchConsumerHeartbeat()
    {
        if (!EnsureInitialized() || !TryLock(kBridgeMutexWaitMs)) {
            return;
        }

        _state->consumerProcessId = _processId;
        _state->consumerHeartbeatMs = GetMonotonicMs();
        Unlock();
    }

    bool KeyboardNativeBridge::HasConsumerHeartbeat(std::uint64_t maxAgeMs) const
    {
        if (!EnsureInitialized() || !TryLock(kBridgeMutexWaitMs)) {
            return false;
        }

        const auto now = GetMonotonicMs();
        const auto heartbeat = _state->consumerHeartbeatMs;
        const auto active = heartbeat != 0 && now >= heartbeat && (now - heartbeat) <= maxAgeMs;

        Unlock();
        return active;
    }

    bool KeyboardNativeBridge::HasRecentProducerHeartbeat(std::uint64_t maxAgeMs) const
    {
        if (!EnsureInitialized() || !TryLock(kBridgeMutexWaitMs)) {
            return false;
        }

        const auto now = GetMonotonicMs();
        const auto heartbeat = _state->producerHeartbeatMs;
        const auto active = heartbeat != 0 && now >= heartbeat && (now - heartbeat) <= maxAgeMs;

        Unlock();
        return active;
    }

    std::uint32_t KeyboardNativeBridge::GetDroppedCount() const
    {
        if (!EnsureInitialized() || !TryLock(kBridgeMutexWaitMs)) {
            return 0;
        }

        const auto dropped = _state->droppedCount;
        Unlock();
        return dropped;
    }

    bool KeyboardNativeBridge::EnsureInitialized() const
    {
        const auto currentProcessId = ::GetCurrentProcessId();
        if (_initialized && _state != nullptr && _processId == currentProcessId) {
            return true;
        }

        if (_initAttempted && _processId == currentProcessId) {
            return _initialized;
        }

        _initAttempted = true;
        _initialized = false;
        _processId = currentProcessId;

        const auto mappingName = BuildMappingName();
        const auto mutexName = BuildMutexName();

        _mapping = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(sizeof(SharedState)),
            mappingName.c_str());
        if (_mapping == nullptr) {
            return false;
        }

        _state = static_cast<SharedState*>(
            ::MapViewOfFile(_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
        if (_state == nullptr) {
            ::CloseHandle(_mapping);
            _mapping = nullptr;
            return false;
        }

        _mutex = ::CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (_mutex == nullptr) {
            ::UnmapViewOfFile(_state);
            _state = nullptr;
            ::CloseHandle(_mapping);
            _mapping = nullptr;
            return false;
        }

        if (!TryLock(kBridgeMutexWaitMs)) {
            return false;
        }

        if (_state->magic != kBridgeMagic || _state->version != kBridgeVersion || _state->ownerProcessId != _processId) {
            std::memset(_state, 0, sizeof(SharedState));
            _state->magic = kBridgeMagic;
            _state->version = kBridgeVersion;
            _state->ownerProcessId = _processId;
        }

        Unlock();
        _initialized = true;
        return true;
    }

    bool KeyboardNativeBridge::EnqueueCommand(KeyboardBridgeCommandType type, std::uint8_t scancode)
    {
        if (!EnsureInitialized() || !TryLock(kBridgeMutexWaitMs)) {
            return false;
        }

        _state->producerProcessId = _processId;
        _state->producerHeartbeatMs = GetMonotonicMs();

        if (_state->count >= kBridgeCapacity) {
            _state->head = (_state->head + 1) % kBridgeCapacity;
            --_state->count;
            ++_state->droppedCount;
        }

        auto& slot = _state->commands[_state->tail];
        slot.type = type;
        slot.scancode = scancode;
        slot.reserved = 0;
        slot.sequence = ++_producerSequence;
        slot.timestampMs = GetMonotonicMs();

        _state->tail = (_state->tail + 1) % kBridgeCapacity;
        ++_state->count;

        Unlock();
        return true;
    }

    bool KeyboardNativeBridge::TryLock(DWORD timeoutMs) const
    {
        if (_mutex == nullptr) {
            return false;
        }

        const auto waitResult = ::WaitForSingleObject(_mutex, timeoutMs);
        return waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED;
    }

    void KeyboardNativeBridge::Unlock() const
    {
        if (_mutex != nullptr) {
            ::ReleaseMutex(_mutex);
        }
    }

    std::wstring KeyboardNativeBridge::BuildMappingName() const
    {
        return std::wstring(L"Local\\DualPadKeyboardBridge-") + std::to_wstring(::GetCurrentProcessId());
    }

    std::wstring KeyboardNativeBridge::BuildMutexName() const
    {
        return std::wstring(L"Local\\DualPadKeyboardBridgeMutex-") + std::to_wstring(::GetCurrentProcessId());
    }
}
