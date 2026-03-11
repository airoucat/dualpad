#pragma once

#include "input/InputContext.h"
#include "input/RuntimeConfig.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace RE
{
    class BSWin32KeyboardDevice;
}

namespace REX::W32
{
    struct DIDEVICEOBJECTDATA;
    struct IDirectInputDevice8A;
}

namespace dualpad::input::backend
{
    class KeyboardNativeBackend
    {
    public:
        static KeyboardNativeBackend& GetSingleton();

        void Install();
        bool IsInstalled() const;
        bool IsRouteActive() const;

        void Reset();
        bool CanHandleAction(std::string_view actionId) const;
        bool PulseAction(std::string_view actionId, InputContext context);
        bool QueueAction(std::string_view actionId, bool pressed, float heldSeconds, InputContext context);

        void InjectControlSemantics(RE::BSWin32KeyboardDevice& device, float timeDelta);
        void InjectDiObjDataEvents(
            RE::BSWin32KeyboardDevice& device,
            REX::W32::IDirectInputDevice8A* directInputDevice,
            std::uint32_t& numEvents,
            REX::W32::DIDEVICEOBJECTDATA* eventBuffer);
        void ObservePostEventState(RE::BSWin32KeyboardDevice& device);
        UpstreamKeyboardHookMode GetHookMode() const;

    private:
        struct ActiveKeyboardAction
        {
            std::uint8_t scancode{ 0 };
            bool viaBridge{ false };
        };

        struct TransparentStringHash
        {
            using is_transparent = void;

            std::size_t operator()(std::string_view value) const noexcept
            {
                return std::hash<std::string_view>{}(value);
            }

            std::size_t operator()(const std::string& value) const noexcept
            {
                return (*this)(std::string_view(value));
            }

            std::size_t operator()(const char* value) const noexcept
            {
                return (*this)(std::string_view(value));
            }
        };

        KeyboardNativeBackend() = default;

        std::optional<std::uint8_t> ResolveScancode(std::string_view actionId, InputContext context) const;
        bool IsTextEntryActive() const;
        bool IsTextSafeScancode(std::uint8_t scancode) const;
        void StagePulseLocked(std::uint8_t scancode);
        bool IsDebugLoggingEnabled() const;
        void ConsumeDesiredStateLocked(
            std::array<std::uint8_t, 256>& desiredCounts,
            std::array<std::uint8_t, 256>& pendingPulseCounts,
            std::array<bool, 256>& syntheticPrevDown);
        void MarkProbeScancodeLocked(std::uint8_t scancode);

        std::mutex _mutex;
        std::array<std::uint8_t, 256> _desiredRefCounts{};
        std::array<std::uint8_t, 256> _bridgeDesiredRefCounts{};
        std::array<std::uint8_t, 256> _pendingPulseCounts{};
        std::array<bool, 256> _syntheticLatchedDown{};
        std::array<bool, 256> _pendingPostEventProbe{};
        bool _pendingNativeGlobalDebug{ false };
        std::unordered_map<std::string, ActiveKeyboardAction, TransparentStringHash, std::equal_to<>> _activeActionScancodes{};
        bool _attemptedInstall{ false };
        bool _installed{ false };
        bool _loggedUnsupportedRuntime{ false };
    };
}
