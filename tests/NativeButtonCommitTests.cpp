#include "pch.h"

#include "input/Action.h"
#include "input/PadProfile.h"
#include "input/XInputButtonSerialization.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/backend/NativeActionDescriptor.h"
#include "input/backend/NativeDigitalPolicyResolver.h"

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

    PlannedAction MakePulseAction(
        PlannedActionPhase phase,
        ActionLifecyclePolicy lifecyclePolicy,
        std::string_view actionId = actions::MenuConfirm,
        NativeControlCode outputCode = NativeControlCode::MenuConfirm)
    {
        const auto digitalPolicy = ResolveNativeDigitalPolicy(
            PlannedBackend::NativeButtonCommit,
            PlannedActionKind::NativeButton,
            ActionOutputContract::Pulse,
            lifecyclePolicy);
        return {
            .backend = PlannedBackend::NativeButtonCommit,
            .kind = PlannedActionKind::NativeButton,
            .phase = phase,
            .context = InputContext::Menu,
            .actionId = std::string(actionId),
            .contract = ActionOutputContract::Pulse,
            .lifecyclePolicy = lifecyclePolicy,
            .outputCode = static_cast<std::uint32_t>(outputCode),
            .digitalPolicy = digitalPolicy,
            .minDownMs = ResolveNativeMinDownMs(digitalPolicy),
            .contextEpoch = 42
        };
    }

    PlannedAction MakeDeferredPulseAction(
        PlannedActionPhase phase,
        std::string_view actionId = actions::MenuConfirm,
        NativeControlCode outputCode = NativeControlCode::MenuConfirm)
    {
        return MakePulseAction(phase, ActionLifecyclePolicy::DeferredPulse, actionId, outputCode);
    }

    PlannedAction MakePulseMinDownAction(
        PlannedActionPhase phase,
        std::string_view actionId = actions::Activate,
        NativeControlCode outputCode = NativeControlCode::Activate)
    {
        return MakePulseAction(phase, ActionLifecyclePolicy::MinDownWindowPulse, actionId, outputCode);
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
        const auto action = MakeDeferredPulseAction(PlannedActionPhase::Press);
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

    void MenuConfirm_DeferredPulse_DoesNotUseMinDownWindow()
    {
        const auto action = MakeDeferredPulseAction(PlannedActionPhase::Press);
        Require(
            action.digitalPolicy == NativeDigitalPolicyKind::DeferredPulse,
            "Menu.Confirm DeferredPulse lifecycle must resolve to DeferredPulse policy");
        Require(action.minDownMs == 0, "Menu.Confirm DeferredPulse must not use a min-down window");
    }

    void GameActivate_MinDownWindowPulse_UsesDefaultMinDown()
    {
        const auto action = MakePulseMinDownAction(PlannedActionPhase::Press);
        Require(
            action.digitalPolicy == NativeDigitalPolicyKind::PulseMinDown,
            "Game.Activate MinDownWindowPulse lifecycle must resolve to PulseMinDown policy");
        Require(action.minDownMs == 40, "Game.Activate MinDownWindowPulse must keep the default min-down window");
    }

    void MenuConfirm_Release_IsNoop_NoTranslateFailed()
    {
        const auto action = MakeDeferredPulseAction(PlannedActionPhase::Release);
        const auto translation = TranslatePlannedActionForNativeButtonCommit(action);
        Require(
            translation.kind == NativeButtonCommitTranslationKind::Noop,
            "Menu.Confirm DeferredPulse release must be an explicit no-op");
        Require(
            translation.requestKind == PollCommitRequestKind::None,
            "DeferredPulse release no-op must not queue a commit request");
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
        MenuConfirm_DeferredPulse_DoesNotUseMinDownWindow();
        GameActivate_MinDownWindowPulse_UsesDefaultMinDown();
        MenuConfirm_Release_IsNoop_NoTranslateFailed();
        PulseMinDown_Release_DoesNotReturnFalse();
        PulseMinDown_PressThenRelease_DoesNotStick();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
