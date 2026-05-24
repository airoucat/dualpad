#include "pch.h"

#include "input_v2/gameplay/GameplayPresentationPublisher.h"
#include "input_v2/gameplay/GameplayProjectionFrame.h"
#include "input_v2/gameplay/RecoveryPlan.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace actions = dualpad::input_v2::actions;
    namespace gameplay = dualpad::input_v2::gameplay;
    namespace presentation = dualpad::input_v2::presentation;
    namespace backend = dualpad::input::backend;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    actions::KernelFrame Kernel(std::uint32_t contextRevision = 7)
    {
        actions::KernelFrame frame{};
        frame.facts.manifestEpoch = 42;
        frame.facts.contextRevision = contextRevision;
        frame.facts.monotonicUs = 10'000;
        frame.state.cleanBoundaryBaseline = true;
        frame.kernelRevision = 99;
        return frame;
    }

    actions::ResolvedActionFrame Resolved(std::uint32_t contextRevision = 7)
    {
        actions::ResolvedActionFrame resolved{};
        resolved.manifestEpoch = 42;
        resolved.contextRevision = contextRevision;
        return resolved;
    }

    void RunFrozenFrameShapeTests()
    {
        gameplay::GameplayProjectionFrame frame{};
        Require(frame.context == gameplay::LegacyInputContextCompat::Gameplay, "GameplayProjectionFrame context must use LegacyInputContextCompat");
        Require(frame.gamepadPlan.transientDigital.items.size() == 32, "transient native command capacity must be fixed at 32");
        Require(frame.gamepadPlan.sustainedDigital.items.size() == 8, "sustained native command capacity must be fixed at 8");
        Require(frame.helperPlan.commands.items.size() == 24, "helper command capacity must be fixed at 24");
        Require(frame.recoveryPlan.mode == gameplay::RecoveryMode::None, "default recovery mode must be None");
        Require(frame.presentationPlan.engineOwner == presentation::PresentationOwner::KeyboardMouse, "default engine owner must be KeyboardMouse");
    }

    void RunRecoveryPlanTests()
    {
        const auto soft = gameplay::BuildRecoveryPlan(gameplay::GameplayRecoveryInput{
            .softResyncRequested = true,
            .cleanFrame = true
        });
        Require(soft.mode == gameplay::RecoveryMode::SoftResyncOutputs, "soft resync must map to SoftResyncOutputs");
        Require(soft.resetNativeCommitBackend, "soft resync must reset native output");
        Require(soft.resetKeyboardHelperBackend, "soft resync must reset helper output");
        Require(soft.resetSustainedDigitalAggregator, "soft resync must reset sustained aggregator");
        Require(soft.clearProjectionStickyOwners, "soft resync must clear projection sticky owners");
        Require(!soft.clearRecoveryBaseline, "soft resync must not clear recovery baseline");
        Require(soft.commitCleanRecoveryBaselineAfterApply, "clean soft resync frame must commit clean baseline after apply");

        const auto hard = gameplay::BuildRecoveryPlan(gameplay::GameplayRecoveryInput{
            .hardResetRequested = true,
            .cleanFrame = true
        });
        Require(hard.mode == gameplay::RecoveryMode::HardResetOutputs, "hard reset must map to HardResetOutputs");
        Require(hard.clearRecoveryBaseline, "hard reset must clear recovery baseline");
        Require(hard.commitCleanRecoveryBaselineAfterApply, "clean hard reset frame must commit clean baseline after apply");

        const auto order = gameplay::BuildRecoveryExecutionPlan(hard);
        const std::vector<gameplay::RecoveryExecutionStep> expected{
            gameplay::RecoveryExecutionStep::ClearOutputState,
            gameplay::RecoveryExecutionStep::ClearSustainedAggregator,
            gameplay::RecoveryExecutionStep::ClearProjectionStickyOwners,
            gameplay::RecoveryExecutionStep::ApplyOutputPlans,
            gameplay::RecoveryExecutionStep::CommitCleanRecoveryBaseline
        };
        Require(order == expected, "RecoveryPlan execution order must be hard-reset safe and fixed");
    }

    void RunProjectionClassificationAndGateTests()
    {
        auto kernel = Kernel();
        kernel.state.controlSamples.push_back(actions::ControlSample{
            .path = actions::ControlPath{ .kind = actions::ControlPathKind::AnalogAxis1D, .code = 200 },
            .down = true,
            .scalar = 0.8f,
            .timestampUs = 10'000
        });

        auto resolved = Resolved();
        resolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = "Game.Jump",
            .bindingId = 1,
            .phase = actions::ActionPhase::Press,
            .timestampUs = 10'000
        });
        resolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = "Game.Sprint",
            .bindingId = 2,
            .phase = actions::ActionPhase::Hold,
            .timestampUs = 10'000
        });
        resolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = "VirtualKey.42",
            .bindingId = 3,
            .phase = actions::ActionPhase::Press,
            .timestampUs = 10'000
        });

        gameplay::GameplayProjectionFrame previous{};
        previous.digitalOwner = gameplay::ChannelOwner::Gamepad;

        const auto projected = gameplay::ResolveGameplayProjection(
            kernel,
            resolved,
            gameplay::GameplayPolicy{ .keyboardMouseDigitalActive = true },
            previous,
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });

        Require(projected.contextRevision == 7, "projection must carry PH2 contextRevision");
        Require(projected.digitalOwner == gameplay::ChannelOwner::KeyboardMouse, "keyboard/mouse transient digital evidence must own transient digital");
        Require(
            projected.gatePlan.transientDigitalGate == gameplay::DigitalGateMode::CancelAndSuppressNewTransient,
            "Gamepad -> KeyboardMouse transient handoff must cancel and suppress new gamepad transient commands");
        Require(projected.gamepadPlan.transientDigital.count == 0, "gated Jump transient must not enter native transient output plan");
        Require(projected.gamepadPlan.sustainedDigital.count == 1, "Sprint must be sustained and must not be governed by DigitalOwner");
        Require(
            projected.gamepadPlan.sustainedDigital.items[0].control == backend::NativeControlCode::Sprint,
            "Sprint sustained output must keep native Sprint control");
        Require(
            (projected.gamepadPlan.sustainedDigital.items[0].activeSourceMask &
             static_cast<std::uint8_t>(gameplay::SustainedSourceBit::GamepadResolved)) != 0,
            "Sprint sustained output must include gamepad resolved source bit");
        Require(projected.helperPlan.commands.count == 1, "Keyboard helper output must bypass DigitalOwner transient gate");
        Require(projected.helperPlan.commands.items[0].helperCode == 42, "VirtualKey helper code must come from stable action id payload");
        Require(
            projected.presentationPlan.engineOwner == presentation::PresentationOwner::KeyboardMouse,
            "presentation plan engineOwner must follow gameplay projection owner result");
    }

    void RunOverflowFailClosedTests()
    {
        auto resolved = Resolved();
        for (std::uint32_t index = 0; index < 33; ++index) {
            resolved.changes.push_back(actions::ActionPhaseChange{
                .actionId = "Game.Jump",
                .bindingId = index + 1,
                .phase = actions::ActionPhase::Press,
                .timestampUs = 10'000 + index
            });
        }

        const auto projected = gameplay::ResolveGameplayProjection(
            Kernel(),
            resolved,
            gameplay::GameplayPolicy{},
            gameplay::GameplayProjectionFrame{},
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });

        Require(projected.gamepadPlan.transientDigital.count == 0, "overflow must clear transient commands instead of partial apply");
        Require(projected.gamepadPlan.sustainedDigital.count == 0, "overflow must clear sustained commands");
        Require(projected.helperPlan.commands.count == 0, "overflow must clear helper commands");
        Require(projected.recoveryPlan.mode == gameplay::RecoveryMode::HardResetOutputs, "overflow must force hard reset");
        Require(projected.helperPlan.enqueueBridgeResetBeforeApply, "overflow must enqueue helper bridge reset");
        Require(projected.reasons.recovery == gameplay::GameplayReasonCode::HardReset, "overflow must explain hard reset recovery");
    }

    void RunPresentationPublisherTests()
    {
        gameplay::GameplayPresentationPublisher publisher;
        gameplay::GameplayProjectionFrame frame{};
        frame.presentationPlan.engineOwner = presentation::PresentationOwner::Gamepad;
        frame.presentationPlan.menuEntryOwner = presentation::PresentationOwner::Gamepad;
        frame.presentationPlan.reason = presentation::GameplayPresentationReasonCode::CarryDigitalOwner;

        const auto blocked = publisher.PublishAfterOutputApply(frame, 10, false);
        Require(blocked.gameplayPresentationRevision == 0, "publisher must not publish before output apply succeeds");

        const auto first = publisher.PublishAfterOutputApply(frame, 11, true);
        Require(first.engineOwner == presentation::PresentationOwner::Gamepad, "publisher must publish projection engine owner");
        Require(first.menuEntryOwner == presentation::PresentationOwner::Gamepad, "publisher must publish clean gameplay menu entry owner");
        Require(first.gameplayPresentationRevision == 1, "first changed publish must advance revision once");
        Require(first.publishedTick == 11, "published tick must come after output apply tick");

        frame.recoveryPlan.mode = gameplay::RecoveryMode::HardResetOutputs;
        frame.recoveryPlan.commitCleanRecoveryBaselineAfterApply = true;
        frame.presentationPlan.reason = presentation::GameplayPresentationReasonCode::RecoveryRepublish;
        const auto republished = publisher.PublishAfterOutputApply(frame, 12, true);
        Require(republished.gameplayPresentationRevision == 2, "hard reset clean baseline republish must advance revision");
        Require(
            republished.reason == presentation::GameplayPresentationReasonCode::RecoveryRepublish,
            "hard reset clean baseline must publish RecoveryRepublish reason");
    }
}

int main()
{
    try {
        RunFrozenFrameShapeTests();
        RunRecoveryPlanTests();
        RunProjectionClassificationAndGateTests();
        RunOverflowFailClosedTests();
        RunPresentationPublisherTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
