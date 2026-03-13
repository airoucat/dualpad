#pragma once

#include "input/InputContext.h"
#include "input/RuntimeConfig.h"
#include "input/backend/ActionLifecycleBackend.h"
#include "input/backend/ActionOutputContract.h"
#include "input/backend/DesiredKeyboardState.h"

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
    class KeyboardNativeBackend final : public IActionLifecycleBackend
    {
    public:
        static KeyboardNativeBackend& GetSingleton();

        void Install();
        bool IsInstalled() const;

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

        void InjectControlSemantics(RE::BSWin32KeyboardDevice& device, float timeDelta);
        void InjectDiObjDataEvents(
            RE::BSWin32KeyboardDevice& device,
            REX::W32::IDirectInputDevice8A* directInputDevice,
            std::uint32_t& numEvents,
            REX::W32::DIDEVICEOBJECTDATA* eventBuffer);
        void ObservePostEventState(RE::BSWin32KeyboardDevice& device);
        UpstreamKeyboardHookMode GetHookMode() const;

    private:
        enum class ScheduledPulsePhase : std::uint8_t
        {
            Idle = 0,
            Down,
            Gap
        };

        struct ActiveKeyboardAction
        {
            std::uint8_t scancode{ 0 };
            bool viaBridge{ false };
            ActionOutputContract contract{ ActionOutputContract::Pulse };
            bool sourceDown{ false };
            ScheduledPulsePhase scheduledPulsePhase{ ScheduledPulsePhase::Idle };
            std::uint8_t scheduledPhaseRemainingConsumes{ 0 };
            std::uint8_t queuedScheduledPulses{ 0 };
            float nextRepeatAtHeldSeconds{ 0.0f };
        };

        struct DeferredKeyboardAction
        {
            ActionOutputContract contract{ ActionOutputContract::None };
            InputContext context{ InputContext::Gameplay };
            bool sourceDown{ false };
            float heldSeconds{ 0.0f };
            bool pendingTriggerPulse{ false };
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
        void StageWindowedPulseLocked(std::uint8_t scancode);
        void StageTransactionalPulseLocked(std::uint8_t scancode);
        bool SubmitScheduledPulseActionState(
            std::string_view actionId,
            ActionOutputContract contract,
            bool pressed,
            float heldSeconds,
            InputContext context);
        void QueueScheduledPulseLocked(ActiveKeyboardAction& action);
        void AppendScheduledPulseStateLocked(DesiredKeyboardState& desiredState);
        void AdvanceScheduledPulseActionsLocked();
        bool IsDebugLoggingEnabled() const;
        void ConsumeDesiredStateLocked(
            DesiredKeyboardState& desiredState,
            std::array<bool, 256>& syntheticPrevDown);
        void MarkProbeScancodeLocked(std::uint8_t scancode);
        bool CanEmitActionNow(std::string_view actionId, InputContext context) const;
        void FlushDeferredActionsIfReady();
        void StageDeferredTriggerLocked(
            std::string_view actionId,
            ActionOutputContract contract,
            InputContext context);
        void StageDeferredStateLocked(
            std::string_view actionId,
            ActionOutputContract contract,
            bool pressed,
            float heldSeconds,
            InputContext context);
        void SuspendActiveActionLocked(std::string_view actionId);

        std::mutex _mutex;
        DesiredKeyboardState _localDesiredState{};
        std::array<std::uint8_t, 256> _bridgeDesiredRefCounts{};
        std::array<bool, 256> _syntheticLatchedDown{};
        std::array<bool, 256> _pendingPostEventProbe{};
        bool _pendingNativeGlobalDebug{ false };
        std::unordered_map<std::string, ActiveKeyboardAction, TransparentStringHash, std::equal_to<>> _activeActionScancodes{};
        std::unordered_map<std::string, DeferredKeyboardAction, TransparentStringHash, std::equal_to<>> _deferredActions{};
        bool _attemptedInstall{ false };
        bool _installed{ false };
        bool _loggedUnsupportedRuntime{ false };
    };
}
