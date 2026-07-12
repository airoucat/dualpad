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
#include <vector>

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

    class RecordingEmitter final : public IPollCommitEmitter
    {
    public:
        EmitResult Emit(const EmitRequest& request) override
        {
            edges.push_back(request.edge);
            return { .submitted = true };
        }

        std::vector<EmitEdge> edges;
    };

    const PollCommitSlot& SprintSlot(const PollCommitCoordinator& coordinator)
    {
        for (const auto& slot : coordinator.Slots()) {
            if (slot.actionId == actions::Sprint) {
                return slot;
            }
        }
        throw std::runtime_error("Sprint slot must exist");
    }

    void Sprint_FullContributorMask_MaterializesOneBridgeWithoutHandoffGap()
    {
        constexpr auto gamepad = static_cast<std::uint8_t>(HeldContributor::Gamepad);
        constexpr auto keyboard = static_cast<std::uint8_t>(HeldContributor::KeyboardMouse);
        constexpr auto mouse = static_cast<std::uint8_t>(HeldContributor::MousePhysical);
        PollCommitCoordinator coordinator;
        RecordingEmitter emitter;
        std::uint64_t generation = 1;
        std::uint64_t nowUs = 1'000;

        const auto sync = [&](std::uint8_t mask, bool bridge) {
            coordinator.BeginFrame(InputContext::Gameplay, 7, nowUs, generation);
            Require(coordinator.SyncHeldContributors(
                    actions::Sprint,
                    NativeControlCode::Sprint,
                    PollCommitMode::Hold,
                    mask,
                    bridge,
                    7),
                "full Sprint contributor mask must synchronize atomically");
            coordinator.Tick(nowUs, true);
            coordinator.Flush(emitter, nowUs);
            ++generation;
            nowUs += 1'000;
        };

        sync(gamepad, true);
        sync(static_cast<std::uint8_t>(gamepad | keyboard), true);
        sync(keyboard, true);
        sync(0, false);
        Require(emitter.edges.size() == 2 &&
                emitter.edges[0] == EmitEdge::Down &&
                emitter.edges[1] == EmitEdge::Up,
            "G -> K handoff must keep one virtual bridge and emit one final release");
        Require(SprintSlot(coordinator).emittedDownCount == 1 &&
                SprintSlot(coordinator).emittedUpCount == 1,
            "G -> K bridge must not introduce a handoff gap or duplicate edge");

        coordinator.Reset();
        emitter.edges.clear();
        sync(keyboard, false);
        Require(emitter.edges.empty(), "K-only Sprint must not synthesize a virtual press");
        sync(static_cast<std::uint8_t>(keyboard | gamepad), true);
        sync(gamepad, true);
        sync(0, false);
        Require(emitter.edges.size() == 2 &&
                emitter.edges.front() == EmitEdge::Down &&
                emitter.edges.back() == EmitEdge::Up,
            "K -> G handoff must create one bridge and release it once without a gap");

        coordinator.Reset();
        emitter.edges.clear();
        sync(static_cast<std::uint8_t>(keyboard | mouse), false);
        const auto& physicalOnly = SprintSlot(coordinator);
        Require(physicalOnly.heldContributorMask == static_cast<std::uint8_t>(keyboard | mouse) &&
                !physicalOnly.virtualBridgeDesired && emitter.edges.empty(),
            "Keyboard and Mouse Sprint contributors must remain independent physical bits");

        coordinator.Reset();
        emitter.edges.clear();
        sync(gamepad, false);
        Require(emitter.edges.empty() &&
                SprintSlot(coordinator).activeHeldEmitter == HeldEmitterSource::None,
            "fail-closed G mask without a bridge must not materialize or manage virtual Sprint");
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
        Sprint_FullContributorMask_MaterializesOneBridgeWithoutHandoffGap();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
