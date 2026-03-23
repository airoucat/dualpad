#pragma once

#include "input/InputContext.h"
#include "input/backend/ActionLifecycleBackend.h"
#include "input/backend/ActionOutputContract.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace dualpad::input::backend
{
    class KeyboardHelperBackend final : public IActionLifecycleBackend
    {
    public:
        static KeyboardHelperBackend& GetSingleton();

        void Install();
        bool IsInstalled() const;
        bool HasProxyDllInGameRoot() const;
        bool HasActiveBridgeConsumer() const;
        bool ShouldExposeModEventConfiguration() const;
        bool IsModEventTransportReady() const;

        void Reset() override;
        bool IsRouteActive() const override;
        bool CanHandleAction(std::string_view actionId) const override;
        bool TriggerAction(std::string_view actionId, ActionOutputContract contract, InputContext context) override;
        bool SubmitActionState(
            std::string_view actionId,
            ActionOutputContract contract,
            bool pressed,
            float heldSeconds,
            InputContext context) override;

    private:
        struct ActiveKeyboardAction
        {
            std::uint8_t scancode{ 0 };
            ActionOutputContract contract{ ActionOutputContract::Pulse };
            bool sourceDown{ false };
            float nextRepeatAtHeldSeconds{ 0.0f };
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

        KeyboardHelperBackend() = default;

        std::optional<std::uint8_t> ResolveScancode(std::string_view actionId, InputContext context) const;
        bool SubmitScheduledPulseActionState(
            std::string_view actionId,
            ActionOutputContract contract,
            bool pressed,
            float heldSeconds,
            InputContext context);
        bool IsDebugLoggingEnabled() const;

        std::mutex _mutex;
        std::array<std::uint8_t, 256> _bridgeDesiredRefCounts{};
        std::unordered_map<std::string, ActiveKeyboardAction, TransparentStringHash, std::equal_to<>> _activeActions{};
        bool _attemptedInstall{ false };
        bool _installed{ false };
    };
}
