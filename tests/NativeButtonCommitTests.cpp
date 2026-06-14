#include "pch.h"

#include "input/Action.h"
#include "input/PadProfile.h"
#include "input/XInputButtonSerialization.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/backend/NativeActionDescriptor.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace dualpad::input;
    using namespace dualpad::input::backend;

    void Require(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    PlannedAction MakePulseMinDownAction(
        PlannedActionPhase phase,
        std::string_view actionId = actions::MenuConfirm,
        NativeControlCode outputCode = NativeControlCode::MenuConfirm)
    {
        return {
            .backend = PlannedBackend::NativeButtonCommit,
            .kind = PlannedActionKind::NativeButton,
            .phase = phase,
            .context = InputContext::Menu,
            .actionId = std::string(actionId),
            .contract = ActionOutputContract::Pulse,
            .lifecyclePolicy = ActionLifecyclePolicy::DeferredPulse,
            .outputCode = static_cast<std::uint32_t>(outputCode),
            .digitalPolicy = NativeDigitalPolicyKind::PulseMinDown,
            .minDownMs = 35,
            .contextEpoch = 42
        };
    }

    void RunNativeDigitalGatePolicyContextTests()
    {
        constexpr std::array contexts{
            InputContext::Gameplay,
            InputContext::Menu,
            InputContext::FavoritesMenu,
            InputContext::Console,
            InputContext::Cursor,
            InputContext::Combat
        };

        for (const auto context : contexts) {
            Require(
                IsNativeDigitalGateOpenForContext(context),
                "native digital gate policy must not block context-local output");
        }
    }

    void MenuConfirm_Press_QueuesPulse_EmitsXInputA()
    {
        const auto action = MakePulseMinDownAction(PlannedActionPhase::Press);
        const auto translation = TranslatePlannedActionForNativeButtonCommit(action);
        Require(
            translation.kind == NativeButtonCommitTranslationKind::Request,
            "Menu.Confirm press must translate to a commit request");
        Require(
            translation.mode == PollCommitMode::Pulse,
            "Menu.Confirm press must queue a pulse mode");
        Require(
            translation.requestKind == PollCommitRequestKind::Pulse,
            "Menu.Confirm press must queue a pulse request");

        const auto* descriptor = FindNativeActionDescriptor(actions::MenuConfirm);
        Require(descriptor != nullptr, "Menu.Confirm must have a native descriptor");
        const auto mask = ResolveVirtualPadBitMask(
            descriptor->virtualButtonRoles,
            GetPadBits(GetActivePadProfile()));
        Require(mask == GetPadBits(GetActivePadProfile()).cross, "Menu.Confirm must resolve to the cross pad bit");
        Require((ToXInputButtons(mask) & 0x1000) != 0, "Menu.Confirm cross bit must emit XInput A");
    }

    void MenuConfirm_Release_IsNoop_NoTranslateFailed()
    {
        const auto action = MakePulseMinDownAction(PlannedActionPhase::Release);
        const auto translation = TranslatePlannedActionForNativeButtonCommit(action);
        Require(
            translation.kind == NativeButtonCommitTranslationKind::Noop,
            "Menu.Confirm PulseMinDown release must be an explicit no-op");
        Require(
            translation.requestKind == PollCommitRequestKind::None,
            "PulseMinDown release no-op must not queue a commit request");
    }

    void PulseMinDown_Release_DoesNotReturnFalse()
    {
        const auto action = MakePulseMinDownAction(
            PlannedActionPhase::Release,
            actions::MenuCancel,
            NativeControlCode::MenuCancel);
        const auto translation = TranslatePlannedActionForNativeButtonCommit(action);
        Require(
            translation.kind != NativeButtonCommitTranslationKind::Invalid,
            "PulseMinDown release must not be classified as translate failure");
    }

    void PulseMinDown_PressThenRelease_DoesNotStick()
    {
        const auto press = TranslatePlannedActionForNativeButtonCommit(
            MakePulseMinDownAction(PlannedActionPhase::Press));
        const auto release = TranslatePlannedActionForNativeButtonCommit(
            MakePulseMinDownAction(PlannedActionPhase::Release));

        Require(
            press.kind == NativeButtonCommitTranslationKind::Request &&
                press.requestKind == PollCommitRequestKind::Pulse,
            "PulseMinDown press must create exactly one pulse request");
        Require(
            release.kind == NativeButtonCommitTranslationKind::Noop,
            "PulseMinDown release after press must be a no-op");
        Require(
            release.mode == PollCommitMode::None &&
                release.requestKind == PollCommitRequestKind::None,
            "PulseMinDown release must not create a clear or force-cancel request");
    }
}

int main()
{
    try {
        RunNativeDigitalGatePolicyContextTests();
        MenuConfirm_Press_QueuesPulse_EmitsXInputA();
        MenuConfirm_Release_IsNoop_NoTranslateFailed();
        PulseMinDown_Release_DoesNotReturnFalse();
        PulseMinDown_PressThenRelease_DoesNotStick();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
