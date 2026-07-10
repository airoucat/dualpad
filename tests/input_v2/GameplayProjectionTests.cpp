#include "pch.h"

#include "input/RuntimeConfig.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input_v2/gameplay/DualPadRuntime.h"
#include "input_v2/gameplay/GameplayPresentationPublisher.h"
#include "input_v2/gameplay/GameplayProjectionFrame.h"
#include "input_v2/gameplay/PollOutputAdapter.h"
#include "input_v2/gameplay/RecoveryPlan.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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
    namespace input = dualpad::input;

    void Require(bool condition, std::string_view message);

    class RecordingPollOutputExecutor final : public gameplay::IPollOutputExecutor
    {
    public:
        bool failOnHelperCommand{ false };
        bool failOnAnalogPublish{ false };
        std::vector<gameplay::PollOutputApplyStep> steps;
        std::size_t sustainedCount{ 0 };
        std::size_t transientCount{ 0 };
        std::size_t helperCount{ 0 };

        bool ClearNativeOutput() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ClearNativeOutput);
            return true;
        }

        bool ClearHelperOutput() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ClearHelperOutput);
            return true;
        }

        bool ClearSustainedDigitalAggregator() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ClearSustainedDigitalAggregator);
            return true;
        }

        bool ClearProjectionStickyOwners() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ClearProjectionStickyOwners);
            return true;
        }

        bool ApplyGatePlan(const gameplay::GatePlan&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplyGatePlan);
            return true;
        }

        bool ApplyPreOutputPresentationHandoff(const gameplay::GameplayPresentationPlan&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplyPreOutputPresentationHandoff);
            return true;
        }

        bool ApplySustainedDigital(const gameplay::NativeSustainedCommand&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplySustainedDigital);
            ++sustainedCount;
            return true;
        }

        bool ApplyTransientDigital(const gameplay::NativeTransientCommand&) override
        {
            const auto gateBeforeTransient = std::find(
                steps.begin(),
                steps.end(),
                gameplay::PollOutputApplyStep::ApplyGatePlan) != steps.end();
            Require(gateBeforeTransient, "transient digital must be applied after GatePlan");
            steps.push_back(gameplay::PollOutputApplyStep::ApplyTransientDigital);
            ++transientCount;
            return true;
        }

        bool ApplyHelperCommand(const gameplay::HelperOutputCommand&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplyHelperCommand);
            ++helperCount;
            return !failOnHelperCommand;
        }

        bool PublishAnalogState(const gameplay::ProjectedAnalogState&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::PublishAnalogState);
            return !failOnAnalogPublish;
        }

        bool CommitCleanRecoveryBaseline() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::CommitCleanRecoveryBaseline);
            return true;
        }
    };

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
        Require(!soft.resetNativeCommitBackend, "SequenceGapWithoutDroppedDigitalEdges_IsSoftGap must not reset native output");
        Require(!soft.resetKeyboardHelperBackend, "SoftGap must not reset helper output");
        Require(!soft.resetSustainedDigitalAggregator, "SoftGap must not reset sustained aggregator");
        Require(!soft.clearProjectionStickyOwners, "SoftGap must not clear projection sticky owners");
        Require(!soft.clearRecoveryBaseline, "SoftGap must not clear recovery baseline");
        Require(soft.commitCleanRecoveryBaselineAfterApply, "clean soft resync frame must commit clean baseline after apply");

        const auto softOrder = gameplay::BuildRecoveryExecutionPlan(soft);
        const std::vector<gameplay::RecoveryExecutionStep> expectedSoft{
            gameplay::RecoveryExecutionStep::ApplyOutputPlans,
            gameplay::RecoveryExecutionStep::CommitCleanRecoveryBaseline
        };
        Require(softOrder == expectedSoft, "SoftGap must not enqueue output-clear recovery steps");

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

    void RunMenuContextGamepadOutputTests()
    {
        auto resolved = Resolved();
        resolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = "Menu.Confirm",
            .bindingId = 10,
            .phase = actions::ActionPhase::Press,
            .timestampUs = 10'000
        });
        resolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = "Menu.ScrollDown",
            .bindingId = 11,
            .phase = actions::ActionPhase::Press,
            .timestampUs = 10'000
        });
        resolved.values.push_back(actions::ActionValueSnapshot{
            .actionId = "Menu.LeftStick",
            .kind = actions::ActionValueKind::Axis2D,
            .scalar = 1.0f,
            .x = 0.0f,
            .y = -1.0f,
            .timestampUs = 10'000
        });

        const auto projected = gameplay::ResolveGameplayProjection(
            Kernel(),
            resolved,
            gameplay::GameplayPolicy{ .gameplayContext = false },
            gameplay::GameplayProjectionFrame{},
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });

        Require(projected.context == gameplay::LegacyInputContextCompat::Menu, "menu projection must carry Menu legacy context");
        Require(
            projected.gatePlan.transientDigitalGate == gameplay::DigitalGateMode::Open,
            "menu context must not suppress resolved gamepad menu digital output");
        Require(projected.gamepadPlan.transientDigital.count == 1, "menu digital action must enter native transient output plan");
        Require(
            projected.gamepadPlan.transientDigital.items[0].control == backend::NativeControlCode::MenuConfirm,
            "Menu.Confirm must keep its native menu control");
        Require(
            projected.gamepadPlan.transientDigital.items[0].lifecyclePolicy == backend::ActionLifecyclePolicy::DeferredPulse,
            "Menu.Confirm must keep DeferredPulse lifecycle through gameplay projection");
        Require(
            !projected.gamepadPlan.transientDigital.items[0].gateAware,
            "Menu.Confirm must not be marked gate-aware in menu projection");
        Require(projected.gamepadPlan.sustainedDigital.count == 1, "menu repeat action must enter native sustained output plan");
        Require(
            projected.gamepadPlan.sustainedDigital.items[0].control == backend::NativeControlCode::MenuScrollDown,
            "Menu.ScrollDown must keep its native menu control");
        Require(
            projected.gamepadPlan.sustainedDigital.items[0].lifecyclePolicy == backend::ActionLifecyclePolicy::RepeatOwner,
            "Menu.ScrollDown must keep RepeatOwner lifecycle through gameplay projection");
        Require(projected.gamepadPlan.analog.moveY == -1.0f, "Menu.LeftStick must not be zeroed by non-gameplay ownership gates");
    }

    void RunGameplayActivateKeepsMinDownWindowLifecycleTests()
    {
        auto resolved = Resolved();
        resolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = "Game.Activate",
            .bindingId = 20,
            .phase = actions::ActionPhase::Press,
            .timestampUs = 10'000
        });

        gameplay::GameplayProjectionFrame previous{};
        previous.digitalOwner = gameplay::ChannelOwner::Gamepad;

        const auto projected = gameplay::ResolveGameplayProjection(
            Kernel(),
            resolved,
            gameplay::GameplayPolicy{ .gameplayContext = true },
            previous,
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });

        Require(projected.gamepadPlan.transientDigital.count == 1, "Game.Activate must enter native transient output plan");
        Require(
            projected.gamepadPlan.transientDigital.items[0].control == backend::NativeControlCode::Activate,
            "Game.Activate must keep native Activate control");
        Require(
            projected.gamepadPlan.transientDigital.items[0].lifecyclePolicy == backend::ActionLifecyclePolicy::MinDownWindowPulse,
            "Game.Activate must keep MinDownWindowPulse lifecycle through gameplay projection");
        Require(
            projected.gamepadPlan.transientDigital.items[0].gateAware,
            "Game.Activate must remain gate-aware in gameplay projection");
    }

    gameplay::GameplayProjectionFrame ResolveFavoritesPress()
    {
        auto resolved = Resolved();
        resolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = "Game.Favorites",
            .bindingId = 21,
            .phase = actions::ActionPhase::Press,
            .timestampUs = 10'000
        });

        return gameplay::ResolveGameplayProjection(
            Kernel(),
            resolved,
            gameplay::GameplayPolicy{ .gameplayContext = true },
            gameplay::GameplayProjectionFrame{},
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });
    }

    void RunGameplayFavoritesFailClosedByDefaultTests()
    {
        const auto missingConfig = std::filesystem::temp_directory_path() / "dualpad-missing-runtime-config.ini";
        std::error_code error;
        std::filesystem::remove(missingConfig, error);
        (void)input::RuntimeConfig::GetSingleton().Load(missingConfig);

        const auto projected = ResolveFavoritesPress();
        const auto route = backend::ActionBackendPolicy::Decide("Game.Favorites");
        Require(
            route.reason == backend::ActionRoutingReason::NativeFavoritesDisabled,
            "disabled native Favorites must expose a stable fail-closed reason");
        Require(
            projected.gamepadPlan.transientDigital.count == 0,
            "Game.Favorites native output must fail closed by default");
        Require(
            !projected.presentationPlan.preOutputPresentationHandoff,
            "disabled native Favorites must not request a pre-output presentation handoff");
    }

    void RunGameplayFavoritesOptInPresentationHandoffTests()
    {
        const auto configPath = std::filesystem::temp_directory_path() / "dualpad-native-favorites-enabled.ini";
        {
            std::ofstream config(configPath, std::ios::trunc);
            config << "[Logging]\n";
            config << "log_poll_diagnostics = true\n";
            config << "[Features]\n";
            config << "enable_native_favorites = true\n";
        }
        Require(input::RuntimeConfig::GetSingleton().Load(configPath), "explicit Favorites config must load");
        Require(
            input::RuntimeConfig::GetSingleton().LogPollDiagnostics(),
            "explicit Poll diagnostics config must enable the bounded diagnostic path");
        std::error_code error;
        std::filesystem::remove(configPath, error);

        const auto projected = ResolveFavoritesPress();

        Require(projected.gamepadPlan.transientDigital.count == 1, "Game.Favorites must enter native transient output plan");
        Require(
            projected.gamepadPlan.transientDigital.items[0].control == backend::NativeControlCode::FavoritesCombo,
            "Game.Favorites must keep native Favorites control");
        Require(
            projected.gamepadPlan.transientDigital.items[0].presentationHandoff ==
                backend::NativePresentationHandoff::GameplayMenuEntry,
            "Game.Favorites must carry gameplay menu-entry presentation handoff metadata");
        Require(
            projected.presentationPlan.engineOwner == presentation::PresentationOwner::Gamepad,
            "gamepad menu-entry transient must publish Gamepad engine owner before native output");
        Require(
            projected.presentationPlan.menuEntryOwner == presentation::PresentationOwner::Gamepad,
            "gamepad menu-entry transient must publish Gamepad menu entry owner");
        Require(
            projected.presentationPlan.preOutputPresentationHandoff,
            "gamepad menu-entry transient must request pre-output presentation handoff");

        gameplay::PollOutputAdapter adapter;
        RecordingPollOutputExecutor executor;
        const auto result = adapter.Apply(projected, executor);
        Require(result.outputApplySucceeded, "Favorites handoff output plan must apply");

        const std::vector<gameplay::PollOutputApplyStep> expected{
            gameplay::PollOutputApplyStep::ApplyGatePlan,
            gameplay::PollOutputApplyStep::ApplyPreOutputPresentationHandoff,
            gameplay::PollOutputApplyStep::ApplyTransientDigital,
            gameplay::PollOutputApplyStep::PublishAnalogState,
            gameplay::PollOutputApplyStep::CommitCleanRecoveryBaseline
        };
        Require(executor.steps == expected, "pre-output presentation handoff must run before native Favorites pulse");
        Require(result.steps == expected, "output result must expose pre-output handoff ordering");
    }

    void RunPrimaryPathArbitrationContractTests()
    {
        const auto keyboardMouseWins = gameplay::ResolvePrimaryPathArbitration(gameplay::PrimaryPathArbitrationInput{
            .previousLookOwner = gameplay::ChannelOwner::Gamepad,
            .previousMoveOwner = gameplay::ChannelOwner::Gamepad,
            .previousCombatOwner = gameplay::ChannelOwner::Gamepad,
            .previousDigitalOwner = gameplay::ChannelOwner::Gamepad,
            .gameplayContext = true,
            .gamepadLookActive = true,
            .gamepadMoveActive = true,
            .gamepadCombatActive = true,
            .gamepadTransientDigitalActive = true,
            .mouseLookActive = true,
            .keyboardMoveActive = true,
            .keyboardMouseCombatActive = true,
            .keyboardMouseDigitalActive = true,
            .uiOwner = presentation::PresentationOwner::KeyboardMouse,
            .menuCursorOwner = presentation::CursorOwner::KeyboardMouse
        });
        Require(
            keyboardMouseWins.lookOwner == gameplay::ChannelOwner::KeyboardMouse,
            "primary path table must give mouse look precedence over gamepad look");
        Require(
            keyboardMouseWins.moveOwner == gameplay::ChannelOwner::KeyboardMouse,
            "primary path table must give keyboard move precedence over gamepad move");
        Require(
            keyboardMouseWins.combatOwner == gameplay::ChannelOwner::KeyboardMouse,
            "primary path table must give keyboard/mouse combat precedence over gamepad combat");
        Require(
            keyboardMouseWins.digitalOwner == gameplay::ChannelOwner::KeyboardMouse,
            "primary path table must give keyboard/mouse digital precedence over gamepad digital");
        Require(
            keyboardMouseWins.engineOwner == presentation::PresentationOwner::KeyboardMouse,
            "primary path table must publish a single UI engine owner");
        Require(
            keyboardMouseWins.cursorOwner == presentation::CursorOwner::KeyboardMouse,
            "primary path table must preserve menu cursor owner coherence");

        const auto gamepadReclaims = gameplay::ResolvePrimaryPathArbitration(gameplay::PrimaryPathArbitrationInput{
            .previousLookOwner = gameplay::ChannelOwner::KeyboardMouse,
            .previousMoveOwner = gameplay::ChannelOwner::KeyboardMouse,
            .previousCombatOwner = gameplay::ChannelOwner::KeyboardMouse,
            .previousDigitalOwner = gameplay::ChannelOwner::KeyboardMouse,
            .gameplayContext = true,
            .gamepadLookActive = true,
            .gamepadMoveActive = true,
            .gamepadCombatActive = true,
            .gamepadTransientDigitalActive = true,
            .uiOwner = presentation::PresentationOwner::Gamepad,
            .menuCursorOwner = presentation::CursorOwner::Gamepad
        });
        Require(gamepadReclaims.lookOwner == gameplay::ChannelOwner::Gamepad, "gamepad look must reclaim when no mouse look is active");
        Require(gamepadReclaims.moveOwner == gameplay::ChannelOwner::Gamepad, "gamepad move must reclaim when no keyboard move is active");
        Require(gamepadReclaims.combatOwner == gameplay::ChannelOwner::Gamepad, "gamepad combat must reclaim when no keyboard/mouse combat is active");
        Require(gamepadReclaims.digitalOwner == gameplay::ChannelOwner::Gamepad, "gamepad digital must reclaim when no keyboard/mouse digital is active");
        Require(gamepadReclaims.engineOwner == presentation::PresentationOwner::Gamepad, "gamepad analog path must publish Gamepad engine owner");
        Require(gamepadReclaims.cursorOwner == presentation::CursorOwner::Gamepad, "gamepad menu cursor owner must stay coherent with the table");

        const auto menuContext = gameplay::ResolvePrimaryPathArbitration(gameplay::PrimaryPathArbitrationInput{
            .previousLookOwner = gameplay::ChannelOwner::Gamepad,
            .previousMoveOwner = gameplay::ChannelOwner::Gamepad,
            .previousCombatOwner = gameplay::ChannelOwner::Gamepad,
            .previousDigitalOwner = gameplay::ChannelOwner::Gamepad,
            .gameplayContext = false,
            .gamepadLookActive = true,
            .gamepadMoveActive = true,
            .gamepadCombatActive = true,
            .gamepadTransientDigitalActive = true,
            .uiOwner = presentation::PresentationOwner::KeyboardMouse,
            .menuCursorOwner = presentation::CursorOwner::KeyboardMouse
        });
        Require(menuContext.lookOwner == gameplay::ChannelOwner::KeyboardMouse, "non-gameplay context must not keep gamepad look owner");
        Require(menuContext.moveOwner == gameplay::ChannelOwner::KeyboardMouse, "non-gameplay context must not keep gamepad move owner");
        Require(menuContext.engineOwner == presentation::PresentationOwner::KeyboardMouse, "non-gameplay context must use UI owner");
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

    void RunSoftRecoveryDoesNotClearOutputTests()
    {
        const auto projected = gameplay::ResolveGameplayProjection(
            Kernel(),
            Resolved(),
            gameplay::GameplayPolicy{},
            gameplay::GameplayProjectionFrame{},
            gameplay::GameplayRecoveryInput{
                .softResyncRequested = true,
                .sequenceGapObserved = true,
                .cleanFrame = true
            });

        Require(projected.recoveryPlan.mode == gameplay::RecoveryMode::SoftResyncOutputs, "SoftGap must remain a soft recovery");
        Require(!projected.recoveryPlan.resetNativeCommitBackend, "SoftGap must not clear native output");
        Require(!projected.recoveryPlan.resetKeyboardHelperBackend, "SoftGap must not clear helper output");
        Require(!projected.recoveryPlan.resetSustainedDigitalAggregator, "SoftGap must not clear sustained output");
        Require(!projected.helperPlan.enqueueBridgeResetBeforeApply, "SoftGap must not enqueue helper bridge reset");

        gameplay::PollOutputAdapter adapter;
        RecordingPollOutputExecutor executor;
        const auto result = adapter.Apply(projected, executor);
        Require(result.outputApplySucceeded, "soft recovery frame must still apply output plans");
        const std::vector<gameplay::PollOutputApplyStep> expected{
            gameplay::PollOutputApplyStep::ApplyGatePlan,
            gameplay::PollOutputApplyStep::PublishAnalogState,
            gameplay::PollOutputApplyStep::CommitCleanRecoveryBaseline
        };
        Require(executor.steps == expected, "RuntimeSnapshotSeqGap_WithoutBoundaryChange_DoesNotClearAuthoritativePoll");
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

    gameplay::GameplayProjectionFrame OutputFrameWithNativeHelperAndRecovery()
    {
        gameplay::GameplayProjectionFrame frame{};
        frame.contextRevision = 11;
        frame.recoveryPlan = gameplay::RecoveryPlan{
            .mode = gameplay::RecoveryMode::HardResetOutputs,
            .resetNativeCommitBackend = true,
            .resetKeyboardHelperBackend = true,
            .resetSustainedDigitalAggregator = true,
            .clearProjectionStickyOwners = true,
            .clearRecoveryBaseline = true,
            .commitCleanRecoveryBaselineAfterApply = true
        };
        frame.gatePlan.transientDigitalGate = gameplay::DigitalGateMode::CancelAndSuppressNewTransient;
        frame.gamepadPlan.sustainedDigital.items[0] = gameplay::NativeSustainedCommand{
            .actionId = "Game.Sprint",
            .control = backend::NativeControlCode::Sprint,
            .activeSourceMask = static_cast<std::uint8_t>(gameplay::SustainedSourceBit::GamepadResolved),
            .contract = backend::ActionOutputContract::Hold,
            .contextRevision = 11
        };
        frame.gamepadPlan.sustainedDigital.count = 1;
        frame.gamepadPlan.transientDigital.items[0] = gameplay::NativeTransientCommand{
            .actionId = "Game.Jump",
            .control = backend::NativeControlCode::Jump,
            .phase = actions::ActionPhase::Press,
            .contract = backend::ActionOutputContract::Pulse,
            .gateAware = true,
            .contextRevision = 11
        };
        frame.gamepadPlan.transientDigital.count = 1;
        frame.helperPlan.commands.items[0] = gameplay::HelperOutputCommand{
            .actionId = "VirtualKey.42",
            .kind = gameplay::HelperOutputKind::KeyboardKey,
            .helperCode = 42,
            .phase = actions::ActionPhase::Press,
            .contract = backend::ActionOutputContract::Pulse,
            .contextRevision = 11
        };
        frame.helperPlan.commands.count = 1;
        frame.gamepadPlan.analog.lookX = 0.5f;
        return frame;
    }

    void RunPollOutputAdapterExecutionTests()
    {
        gameplay::PollOutputAdapter adapter;
        RecordingPollOutputExecutor executor;

        const auto result = adapter.Apply(OutputFrameWithNativeHelperAndRecovery(), executor);
        Require(result.outputApplySucceeded, "executor must report success only after every output plan applies");
        Require(executor.sustainedCount == 1, "executor must apply sustainedDigital commands");
        Require(executor.transientCount == 1, "executor must apply transientDigital commands");
        Require(executor.helperCount == 1, "executor must apply helperPlan commands");

        const std::vector<gameplay::PollOutputApplyStep> expected{
            gameplay::PollOutputApplyStep::ClearNativeOutput,
            gameplay::PollOutputApplyStep::ClearHelperOutput,
            gameplay::PollOutputApplyStep::ClearSustainedDigitalAggregator,
            gameplay::PollOutputApplyStep::ClearProjectionStickyOwners,
            gameplay::PollOutputApplyStep::ApplyGatePlan,
            gameplay::PollOutputApplyStep::ApplySustainedDigital,
            gameplay::PollOutputApplyStep::ApplyTransientDigital,
            gameplay::PollOutputApplyStep::ApplyHelperCommand,
            gameplay::PollOutputApplyStep::PublishAnalogState,
            gameplay::PollOutputApplyStep::CommitCleanRecoveryBaseline
        };
        Require(executor.steps == expected, "PollOutputAdapter must apply recovery, gate, native, helper, analog, clean baseline in fixed order");
        Require(result.steps == expected, "PollOutputAdapter result must expose the same fixed order for runtime diagnostics");
    }

    void RunDualPadRuntimePublisherSeamTests()
    {
        gameplay::DualPadRuntime runtime;
        runtime.ResetForTests();

        gameplay::DualPadRuntimeInput input{
            .kernel = Kernel(),
            .resolved = Resolved(),
            .policy = gameplay::GameplayPolicy{},
            .recovery = gameplay::GameplayRecoveryInput{ .hardResetRequested = true, .cleanFrame = true },
            .outputTick = 100
        };

        RecordingPollOutputExecutor successExecutor;
        const auto success = runtime.ProcessGameplayFrameForTests(input, successExecutor);
        Require(success.output.outputApplySucceeded, "runtime must publish only after output apply succeeds");
        Require(
            runtime.GetPublishedGameplayPresentation().gameplayPresentationRevision == 1,
            "runtime owner must publish gameplay presentation after successful hard reset output apply");
        Require(
            successExecutor.steps.front() == gameplay::PollOutputApplyStep::ClearNativeOutput,
            "hard reset recovery must clear native output before applying projection plans");

        gameplay::DualPadRuntime failedRuntime;
        failedRuntime.ResetForTests();
        RecordingPollOutputExecutor failingExecutor;
        failingExecutor.failOnAnalogPublish = true;
        const auto failed = failedRuntime.ProcessGameplayFrameForTests(input, failingExecutor);
        Require(!failed.output.outputApplySucceeded, "runtime must surface failed output apply");
        Require(
            failedRuntime.GetPublishedGameplayPresentation().gameplayPresentationRevision == 0,
            "runtime owner must not publish gameplay presentation when outputApplySucceeded=false");
    }

    void RunCoordinatorAuthorityCutoverTests()
    {
        Require(
            !gameplay::DualPadRuntime::LiveCoordinatorPresentationAuthorityReachable(),
            "legacy gameplay ownership authority must not be reachable from live runtime");
    }
}

int main()
{
    try {
        RunFrozenFrameShapeTests();
        RunRecoveryPlanTests();
        RunProjectionClassificationAndGateTests();
        RunMenuContextGamepadOutputTests();
        RunGameplayActivateKeepsMinDownWindowLifecycleTests();
        RunGameplayFavoritesFailClosedByDefaultTests();
        RunGameplayFavoritesOptInPresentationHandoffTests();
        RunPrimaryPathArbitrationContractTests();
        RunOverflowFailClosedTests();
        RunSoftRecoveryDoesNotClearOutputTests();
        RunPresentationPublisherTests();
        RunPollOutputAdapterExecutionTests();
        RunDualPadRuntimePublisherSeamTests();
        RunCoordinatorAuthorityCutoverTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
