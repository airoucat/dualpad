#include "pch.h"

#include "input/Action.h"
#include "input/RuntimeConfig.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/injection/PollMaterializationReceipt.h"
#include "input/injection/SkyrimCurrentCycleEventAdapter.h"
#include "input_v2/gameplay/DualPadRuntime.h"
#include "input_v2/gameplay/ChannelArbitration.h"
#include "input_v2/gameplay/CurrentCycleGatePlan.h"
#include "input_v2/gameplay/GameplayPresentationPublisher.h"
#include "input_v2/gameplay/GameplayProjectionFrame.h"
#include "input_v2/gameplay/PollOutputAdapter.h"
#include "input_v2/gameplay/RecoveryPlan.h"
#include "input_v2/gameplay/RuntimeInputPublication.h"
#include "input_v2/gameplay/SustainedContributorDecision.h"
#include "input_v2/gameplay/TransientActionGate.h"
#include "input_v2/telemetry/MixedInputEvidence.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace actions = dualpad::input_v2::actions;
    namespace gameplay = dualpad::input_v2::gameplay;
    namespace presentation = dualpad::input_v2::presentation;
    namespace telemetry = dualpad::input_v2::telemetry;
    namespace backend = dualpad::input::backend;
    namespace input = dualpad::input;

    void Require(bool condition, std::string_view message);

    class RecordingPollOutputExecutor final : public gameplay::IPollOutputExecutor
    {
    public:
        bool failOnHelperCommand{ false };
        bool failOnAnalogPublish{ false };
        std::vector<gameplay::PollOutputApplyStep> steps;
        std::optional<gameplay::NativeSustainedCommand> lastSustained;
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

        bool ApplySustainedDigital(const gameplay::NativeSustainedCommand& command) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplySustainedDigital);
            lastSustained = command;
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

    actions::ResolvedActionFrame ResolvedAxes(
        float look,
        float move,
        float leftTrigger = 0.0f,
        float rightTrigger = 0.0f)
    {
        auto resolved = Resolved();
        resolved.values = {
            actions::ActionValueSnapshot{
                .actionId = "Game.Look",
                .kind = actions::ActionValueKind::Axis2D,
                .x = look,
                .timestampUs = 10'000 },
            actions::ActionValueSnapshot{
                .actionId = "Game.Move",
                .kind = actions::ActionValueKind::Axis2D,
                .x = move,
                .timestampUs = 10'000 },
            actions::ActionValueSnapshot{
                .actionId = "Game.LeftTrigger",
                .kind = actions::ActionValueKind::Axis1D,
                .scalar = leftTrigger,
                .timestampUs = 10'000 },
            actions::ActionValueSnapshot{
                .actionId = "Game.RightTrigger",
                .kind = actions::ActionValueKind::Axis1D,
                .scalar = rightTrigger,
                .timestampUs = 10'000 }
        };
        return resolved;
    }

    gameplay::ChannelArbitrationDecision DecideChannel(
        gameplay::ChannelArbitrationState previous,
        bool keyboardMouseActive,
        bool keyboardMouseActivation,
        float gamepadMagnitude,
        std::uint64_t nowUs,
        std::uint64_t lastKeyboardMouseActivityUs,
        std::uint64_t quietWindowUs = 0,
        gameplay::ChannelArbitrationResetMode resetMode = gameplay::ChannelArbitrationResetMode::None)
    {
        return gameplay::ResolveChannelArbitration(gameplay::ChannelArbitrationInput{
            .previous = previous,
            .gameplayContext = true,
            .keyboardMouseActive = keyboardMouseActive,
            .keyboardMouseActivation = keyboardMouseActivation,
            .gamepadMagnitude = gamepadMagnitude,
            .gamepadEnterThreshold = 0.25f,
            .gamepadSustainThreshold = 0.15f,
            .nowUs = nowUs,
            .lastKeyboardMouseActivityUs = lastKeyboardMouseActivityUs,
            .keyboardMouseQuietWindowUs = quietWindowUs,
            .keyboardMouseReason = gameplay::GameplayReasonCode::MouseLookActive,
            .gamepadReason = gameplay::GameplayReasonCode::MeaningfulRightStick,
            .resetMode = resetMode
        });
    }

    void RunPerChannelMixedInputArbitrationTests()
    {
        static_assert(static_cast<std::uint8_t>(gameplay::ChannelOwner::Gamepad) == 0);
        static_assert(static_cast<std::uint8_t>(gameplay::ChannelOwner::KeyboardMouse) == 1);
        static_assert(static_cast<std::uint8_t>(gameplay::ChannelOwner::None) == 2);

        const auto mixed = gameplay::ResolveGameplayProjection(
            Kernel(),
            ResolvedAxes(0.65f, 0.70f),
            gameplay::GameplayPolicy{
                .outputTickUs = 100'000,
                .lastPhysicalMouseMoveOwnerUs = 100'000,
                .mouseLookActive = true,
                .mouseLookActivatedThisFrame = true },
            gameplay::GameplayProjectionFrame{},
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });
        Require(mixed.lookOwner == gameplay::ChannelOwner::KeyboardMouse, "mouse delta must own Look while LS remains independent");
        Require(mixed.moveOwner == gameplay::ChannelOwner::Gamepad, "LS 0.70 must own Move while mouse owns Look");
        Require(mixed.gatePlan.lookGate == gameplay::AnalogGateMode::ZeroedByKeyboardMouse, "mouse-owned Look must gate RS");
        Require(mixed.gatePlan.moveGate == gameplay::AnalogGateMode::Open, "gamepad-owned Move must preserve LS");
        Require(mixed.gamepadPlan.analog.lookX == 0.0f, "gated RS must be neutral");
        Require(mixed.gamepadPlan.analog.moveX == 0.70f, "independent LS must pass");
        Require(mixed.nextArbitration.look.gamepadCandidate, "same-frame physical mouse priority must latch the RS candidate");

        const auto inverse = gameplay::ResolveGameplayProjection(
            Kernel(),
            ResolvedAxes(0.70f, 0.80f),
            gameplay::GameplayPolicy{
                .outputTickUs = 100'000,
                .keyboardMoveActive = true,
                .keyboardMoveActivatedThisFrame = true },
            gameplay::GameplayProjectionFrame{},
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });
        Require(inverse.lookOwner == gameplay::ChannelOwner::Gamepad, "RS must own Look independently of keyboard Move");
        Require(inverse.moveOwner == gameplay::ChannelOwner::KeyboardMouse, "mapped move held must own Move");

        auto lookPrevious = mixed.nextArbitration.look;
        const auto at199ms = DecideChannel(lookPrevious, false, false, 0.16f, 299'000, 100'000, 200'000);
        Require(at199ms.owner == gameplay::ChannelOwner::KeyboardMouse, "mouse quiet window must retain Look at 199 ms");
        const auto at201ms = DecideChannel(at199ms.next, false, false, 0.16f, 301'000, 100'000, 200'000);
        Require(at201ms.owner == gameplay::ChannelOwner::Gamepad, "latched RS must reclaim Look on the first tick after 200 ms");

        const auto unlatched = DecideChannel({}, false, false, 0.24f, 301'000, 100'000, 200'000);
        Require(unlatched.owner == gameplay::ChannelOwner::None, "unlatched sub-enter RS must leave Look neutral");
        const auto enters = DecideChannel(unlatched.next, false, false, 0.26f, 302'000, 100'000, 200'000);
        Require(enters.owner == gameplay::ChannelOwner::Gamepad, "RS must enter only above enter threshold when unlatched");

        const auto moveHeld = DecideChannel({}, true, true, 0.80f, 100'000, 100'000);
        Require(moveHeld.owner == gameplay::ChannelOwner::KeyboardMouse && moveHeld.next.gamepadCandidate,
            "keyboard Move must win and latch an eligible LS candidate");
        const auto moveRelease = DecideChannel(moveHeld.next, false, false, 0.16f, 101'000, 100'000);
        Require(moveRelease.owner == gameplay::ChannelOwner::Gamepad, "latched LS must reclaim on the key-release tick");
        const auto unlatchedMoveRelease = DecideChannel({}, false, false, 0.16f, 101'000, 100'000);
        Require(unlatchedMoveRelease.owner == gameplay::ChannelOwner::None, "unlatched LS at sustain must not enter Move");

        const auto combat = gameplay::ResolveGameplayProjection(
            Kernel(),
            ResolvedAxes(0.70f, 0.80f, 0.9f, 0.8f),
            gameplay::GameplayPolicy{
                .outputTickUs = 100'000,
                .keyboardMouseCombatActive = true,
                .keyboardMouseCombatActivatedThisFrame = true },
            gameplay::GameplayProjectionFrame{},
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });
        Require(combat.combatOwner == gameplay::ChannelOwner::KeyboardMouse, "physical combat held must own Combat");
        Require(combat.gatePlan.leftTriggerGate == gameplay::AnalogGateMode::ZeroedByKeyboardMouse &&
                combat.gatePlan.rightTriggerGate == gameplay::AnalogGateMode::ZeroedByKeyboardMouse,
            "Combat ownership must gate both triggers");
        Require(combat.lookOwner == gameplay::ChannelOwner::Gamepad && combat.moveOwner == gameplay::ChannelOwner::Gamepad,
            "Combat arbitration must not affect Look or Move");

        const auto inactive = DecideChannel({}, false, false, 0.0f, 100'000, 0);
        Require(inactive.owner == gameplay::ChannelOwner::None && inactive.gateGamepad,
            "inactive channel must be None and virtual-neutral");

        gameplay::ChannelArbitrationState gamepadLook{
            .owner = gameplay::ChannelOwner::Gamepad,
            .gamepadCandidate = true };
        gameplay::ChannelArbitrationState keyboardMove{
            .owner = gameplay::ChannelOwner::KeyboardMouse,
            .gamepadCandidate = true,
            .lastKeyboardMouseActivityUs = 90'000 };
        const auto disconnectedLook = DecideChannel(
            gamepadLook, false, false, 0.8f, 100'000, 0, 0,
            gameplay::ChannelArbitrationResetMode::GamepadSource);
        const auto disconnectedMove = DecideChannel(
            keyboardMove, true, false, 0.8f, 100'000, 90'000, 0,
            gameplay::ChannelArbitrationResetMode::GamepadSource);
        Require(disconnectedLook.owner == gameplay::ChannelOwner::None && !disconnectedLook.next.gamepadCandidate,
            "GamepadSource reset must clear gamepad owner and candidate");
        Require(disconnectedMove.owner == gameplay::ChannelOwner::KeyboardMouse &&
                disconnectedMove.next.lastKeyboardMouseActivityUs == 90'000 &&
                !disconnectedMove.next.gamepadCandidate,
            "GamepadSource reset must preserve KBM owner/quiet state and clear candidate");
        const auto globalMove = DecideChannel(
            keyboardMove, true, false, 0.8f, 100'000, 90'000, 0,
            gameplay::ChannelArbitrationResetMode::Global);
        Require(globalMove.owner == gameplay::ChannelOwner::None &&
                globalMove.next.lastKeyboardMouseActivityUs == 0,
            "Global reset must clear every channel state");
    }

    input::PollFrameIdentity ReceiptIdentity(
        std::uint64_t publicationGeneration = 41,
        std::uint32_t packetNumber = 9)
    {
        return input::PollFrameIdentity{
            .publicationGeneration = publicationGeneration,
            .runtimeGeneration = publicationGeneration,
            .packetNumber = packetNumber,
            .inputStateEpoch = 7,
            .gamepadSessionId = 3,
            .contextRevision = 11,
            .controlMapRevision = 5
        };
    }

    void RunPollMaterializationReceiptTests()
    {
        auto& receipts = input::PollMaterializationReceiptStore::GetSingleton();
        receipts.ResetForTests();
        Require(receipts.PublishForTests(input::PollMaterializationReceipt{
                .hookCallSequence = 41,
                .threadId = 1001,
                .identity = ReceiptIdentity(),
                .serializeSucceeded = true }),
            "verified serialize receipt must publish");

        const auto consumed = receipts.ConsumeExact(41, 1001);
        Require(consumed.Succeeded(), "matching sequence/thread must consume receipt once");
        Require(consumed.receipt->identity.publicationGeneration == 41 &&
                consumed.receipt->identity.packetNumber == 9,
            "receipt must freeze the exact materialized Poll identity");

        gameplay::PollOutputPublication::GetSingleton().PublishForTests(gameplay::PollOutputFrame{
            .runtimeGeneration = 42,
            .routeHealth = gameplay::PollOutputRouteHealth::Ready
        });
        Require(consumed.receipt->identity.publicationGeneration == 41,
            "later Poll publication must not rewrite consumed identity");
        Require(receipts.ConsumeExact(41, 1001).failure == input::PollReceiptConsumeFailure::AlreadyConsumed,
            "receipt must be consume-once");
        Require(receipts.ConsumeExact(999, 1001).failure == input::PollReceiptConsumeFailure::Missing,
            "missing receipt must return an exact failure");

        receipts.ResetForTests();
        (void)receipts.PublishForTests(input::PollMaterializationReceipt{
            .hookCallSequence = 50, .threadId = 1001, .identity = ReceiptIdentity(50, 10), .serializeSucceeded = true });
        Require(receipts.ConsumeForThread(2002).failure == input::PollReceiptConsumeFailure::ThreadMismatch,
            "cross-thread receipt consume must fail closed");
        (void)receipts.PublishForTests(input::PollMaterializationReceipt{
            .hookCallSequence = 51, .threadId = 1001, .identity = ReceiptIdentity(51, 11), .serializeSucceeded = true });
        Require(receipts.ConsumeForThread(1001).failure == input::PollReceiptConsumeFailure::Ambiguous,
            "multiple unconsumed receipts for one callback thread must be ambiguous");
    }

    gameplay::CurrentCycleGateInput ExactCurrentCycleInput()
    {
        const auto receipt = input::PollMaterializationReceipt{
            .hookCallSequence = 41,
            .threadId = 1001,
            .identity = ReceiptIdentity(),
            .serializeSucceeded = true
        };
        return gameplay::CurrentCycleGateInput{
            .receipt = receipt,
            .observedIdentity = receipt.identity,
            .physicalFactsComplete = true,
            .routeAvailable = true,
            .consumerOrderProven = true,
            .scratchCapacitySufficient = true
        };
    }

    void RunCurrentCycleGateAndPreparedCommitTests()
    {
        auto inputFrame = ExactCurrentCycleInput();
        inputFrame.physicalLookActivation = true;
        inputFrame.materializedLookEvent = true;
        inputFrame.materializedMoveEvent = true;
        const auto plan = gameplay::BuildCurrentCycleGatePlan(inputFrame);
        Require(plan.failure == gameplay::CurrentCycleGateFailure::None,
            "exact receipt and complete facts must build a current-cycle plan");
        Require(plan.look == gameplay::CurrentCycleEventDisposition::Neutralize &&
                plan.move == gameplay::CurrentCycleEventDisposition::Keep,
            "physical mouse conflict must neutralize RS without modifying LS");
        Require(plan.requiresEventMutation && plan.currentEventWriterCount == 1 &&
                plan.nextPollWriterCount <= 1,
            "current-cycle and next-Poll views must each remain single-writer");

        auto mismatched = inputFrame;
        ++mismatched.observedIdentity.packetNumber;
        const auto mismatchPlan = gameplay::BuildCurrentCycleGatePlan(mismatched);
        Require(mismatchPlan.failure == gameplay::CurrentCycleGateFailure::PollFrameMismatch &&
                !mismatchPlan.requiresEventMutation && mismatchPlan.currentEventWriterCount == 0,
            "Poll identity mismatch must produce zero event mutation");

        const auto expectFailure = [&](auto mutate, gameplay::CurrentCycleGateFailure expected) {
            auto fixture = ExactCurrentCycleInput();
            mutate(fixture);
            const auto failed = gameplay::BuildCurrentCycleGatePlan(fixture);
            Require(failed.failure == expected && !failed.requiresEventMutation &&
                    failed.currentEventWriterCount == 0,
                "current-cycle failure table must return an exact failure with zero mutation");
        };
        expectFailure([](auto& fixture) { ++fixture.observedIdentity.inputStateEpoch; },
            gameplay::CurrentCycleGateFailure::InputStateEpochMismatch);
        expectFailure([](auto& fixture) { ++fixture.observedIdentity.gamepadSessionId; },
            gameplay::CurrentCycleGateFailure::GamepadSessionMismatch);
        expectFailure([](auto& fixture) { ++fixture.observedIdentity.contextRevision; },
            gameplay::CurrentCycleGateFailure::ContextMismatch);
        expectFailure([](auto& fixture) { ++fixture.observedIdentity.controlMapRevision; },
            gameplay::CurrentCycleGateFailure::ControlMapMismatch);
        expectFailure([](auto& fixture) { ++fixture.observedIdentity.orderedCutoffSeq; },
            gameplay::CurrentCycleGateFailure::CutoffMismatch);
        expectFailure([](auto& fixture) { fixture.physicalFactsComplete = false; },
            gameplay::CurrentCycleGateFailure::PhysicalFactsIncomplete);
        expectFailure([](auto& fixture) { fixture.routeAvailable = false; },
            gameplay::CurrentCycleGateFailure::RouteUnavailable);
        expectFailure([](auto& fixture) {
            fixture.mutationCapabilityEnabled = true;
            fixture.consumerOrderProven = false;
        }, gameplay::CurrentCycleGateFailure::ConsumerOrderUnproven);
        expectFailure([](auto& fixture) { fixture.scratchCapacitySufficient = false; },
            gameplay::CurrentCycleGateFailure::ScratchCapacityExceeded);

        const auto noMutationPlan = gameplay::BuildCurrentCycleGatePlan(ExactCurrentCycleInput());
        Require(!noMutationPlan.requiresEventMutation && noMutationPlan.commitCurrentCycleSensitiveState,
            "audited no-mutation plan must allow sensitive-state commit");

        std::vector<input::CurrentCycleEventDescriptor> descriptors{
            { .channel = gameplay::CurrentCycleChannel::Look, .virtualEvent = true },
            { .channel = gameplay::CurrentCycleChannel::Move, .virtualEvent = true }
        };
        input::SkyrimCurrentCycleEventAdapter adapter;
        const auto shadowAudit = adapter.AuditDescriptors(
            descriptors,
            plan,
            input::CurrentCycleAdapterOptions{
                .shadowOnly = true,
                .consumerOrderProven = false,
                .scratchCapacity = 8 });
        Require(shadowAudit.success && !shadowAudit.mutationApplied && shadowAudit.wouldMutateCount == 1,
            "WP5 shadow adapter must audit losing virtual events without mutation");
        Require(descriptors[0].virtualEvent && descriptors[1].virtualEvent,
            "shadow adapter must leave callback-local descriptors unchanged");

        const auto scratchFailure = adapter.AuditDescriptors(
            descriptors,
            plan,
            input::CurrentCycleAdapterOptions{
                .shadowOnly = true,
                .consumerOrderProven = false,
                .scratchCapacity = 1 });
        Require(!scratchFailure.success &&
                scratchFailure.failure == gameplay::CurrentCycleGateFailure::ScratchCapacityExceeded,
            "adapter scratch overflow must fail with zero mutation");

        std::vector<input::CurrentCycleEventDescriptor> middleFixture{
            { .channel = gameplay::CurrentCycleChannel::Move, .virtualEvent = true },
            { .channel = gameplay::CurrentCycleChannel::Look, .virtualEvent = true },
            { .channel = gameplay::CurrentCycleChannel::Combat, .virtualEvent = true }
        };
        const auto middleAudit = adapter.AuditDescriptors(
            middleFixture,
            plan,
            input::CurrentCycleAdapterOptions{
                .shadowOnly = true,
                .scratchCapacity = 8 });
        Require(middleAudit.success && middleAudit.wouldMutateCount == 1,
            "shadow adapter must find a losing virtual event at list middle without touching head/tail");

        gameplay::CurrentCycleSensitiveState previous{};
        previous.channels.look.owner = gameplay::ChannelOwner::KeyboardMouse;
        previous.revision = 7;
        auto proposed = previous;
        proposed.channels.look.owner = gameplay::ChannelOwner::Gamepad;
        proposed.sprint.activeSourceMask = 0x03;
        proposed.sprint.virtualMaterialized = true;
        proposed.revision = 8;

        gameplay::RuntimeInputPublication publication;
        publication.ResetForTests(previous);
        const auto prepared = publication.Prepare(proposed, plan);
        const auto rollback = publication.CommitAfterCurrentCycleAudit(
            prepared.token,
            input::CurrentCycleAdapterAudit{
                .success = false,
                .failure = gameplay::CurrentCycleGateFailure::AdapterFailure,
                .affectedChannels = gameplay::CurrentCycleChannelMask(gameplay::CurrentCycleChannel::Look) });
        Require(!rollback.committed && rollback.failClosedChannels ==
                gameplay::CurrentCycleChannelMask(gameplay::CurrentCycleChannel::Look),
            "adapter failure must roll back and fail closed affected channels");
        Require(publication.GetCommitted().revision == 7 &&
                publication.GetCommitted().sprint.activeSourceMask == 0 &&
                !publication.GetCommitted().sprint.virtualMaterialized,
            "adapter failure must not advance channel/Sprint ledgers");
        Require(publication.CommitAfterCurrentCycleAudit(prepared.token, shadowAudit).alreadyConsumed,
            "prepared commit token must consume exactly once on rollback");

        const auto preparedShadowOnly = publication.Prepare(proposed, plan);
        const auto shadowOnlyRollback = publication.CommitAfterCurrentCycleAudit(
            preparedShadowOnly.token,
            shadowAudit);
        Require(!shadowOnlyRollback.committed &&
                shadowOnlyRollback.failClosedChannels ==
                    gameplay::CurrentCycleChannelMask(gameplay::CurrentCycleChannel::Look) &&
                publication.GetCommitted().revision == 7,
            "shadow-only audit must not commit sensitive state when mutation is required");

        const auto preparedNoMutation = publication.Prepare(proposed, noMutationPlan);
        const auto committed = publication.CommitAfterCurrentCycleAudit(
            preparedNoMutation.token,
            input::CurrentCycleAdapterAudit{ .success = true });
        Require(committed.committed && publication.GetCommitted().revision == 8,
            "successful no-mutation audit must commit proposed sensitive state");

        const gameplay::CurrentCycleCallbackEvidence callbackEvidence{
            .ownerTickToken = 9001,
            .monotonicUs = 123'000,
            .currentInputStateEpoch = 7,
            .currentGamepadSessionId = 3,
            .receipt = input::PollMaterializationReceipt{
                .hookCallSequence = 71,
                .threadId = 1001,
                .identity = ReceiptIdentity(70, 11),
                .serializeSucceeded = true }
        };
        const auto preparedCallback = publication.PrepareCallbackAudit(
            9001,
            plan,
            callbackEvidence);
        Require(preparedCallback.token != 0 &&
                publication.CommitCallbackAudit(preparedCallback.token, shadowAudit),
            "callback-local receipt plan must follow Prepare -> Apply -> Commit");
        Require(!publication.CommitCallbackAudit(preparedCallback.token, shadowAudit),
            "callback-local prepared token must be consume-once");
        const auto publishedCallback = publication.FindCallbackAudit(9001);
        Require(publishedCallback.has_value() &&
                publishedCallback->evidence.receipt.has_value() &&
                publishedCallback->evidence.receipt->hookCallSequence == 71 &&
                publishedCallback->evidence.currentInputStateEpoch == 7,
            "committed callback audit must remain visible only during its callback scope");
        publication.ClearCallbackAudit(9001);
        Require(!publication.FindCallbackAudit(9001).has_value(),
            "callback audit must be cleared before the callback returns");
    }

    void RunTransientActionGateTests()
    {
        const gameplay::TransientDedupKey jump{
            .actionId = "Game.Jump",
            .materializationToken = 77,
            .contextRevision = 11
        };
        const auto duplicatePress = gameplay::ResolveTransientActionGate(gameplay::TransientActionGateInput{
            .key = jump,
            .physicalPress = true,
            .virtualPress = true,
            .virtualDownVisible = true
        });
        Require(duplicatePress.physicalDisposition == gameplay::TransientGateDisposition::Keep &&
                duplicatePress.virtualDisposition == gameplay::TransientGateDisposition::Cancel,
            "physical and virtual transient with one key must materialize once");

        const auto physicalRelease = gameplay::ResolveTransientActionGate(gameplay::TransientActionGateInput{
            .previous = duplicatePress.next,
            .key = jump,
            .physicalRelease = true,
            .virtualPress = true
        });
        Require(physicalRelease.virtualDisposition == gameplay::TransientGateDisposition::Keep,
            "physical release must not suppress a new gamepad transient press");
    }

    void RunSprintContributorDecisionTests()
    {
        using gameplay::SustainedContributorBit;
        const auto bit = [](SustainedContributorBit source) {
            return gameplay::SustainedContributorMask(source);
        };
        const auto gamepad = bit(SustainedContributorBit::Gamepad);
        const auto keyboard = bit(SustainedContributorBit::KeyboardPhysical);
        const auto mouse = bit(SustainedContributorBit::MousePhysical);

        gameplay::SustainedContributorState state{};
        const auto step = [&](std::uint8_t mask) {
            const auto decision = gameplay::ResolveSustainedContributor(gameplay::SustainedContributorInput{
                .previous = state,
                .activeSourceMask = mask,
                .gamepadEventOrdinal = 30,
                .keyboardEventOrdinal = 10,
                .mouseEventOrdinal = 20
            });
            state = decision.next;
            return decision;
        };

        const auto g = step(gamepad);
        const auto gk = step(gamepad | keyboard);
        const auto k = step(keyboard);
        const auto none = step(0);
        Require(g.aggregateHeld && g.virtualBridgeDesired &&
                g.next.activeSourceMask == gamepad,
            "G press must establish the virtual Sprint bridge");
        Require(gk.aggregateHeld && gk.virtualBridgeDesired &&
                gk.joiningPressSuppressionMask == keyboard,
            "K joining an active G Sprint must be suppressed without dropping aggregate hold");
        Require(k.aggregateHeld && k.virtualBridgeDesired &&
                k.nonFinalReleaseSuppressionMask == gamepad,
            "G non-final release must keep the materialized bridge while K remains held");
        Require(!none.aggregateHeld && !none.virtualBridgeDesired &&
                none.finalRelease && none.releaseToken != 0,
            "last Sprint contributor must produce one final virtual release token");

        state = {};
        const auto kOnly = step(keyboard);
        const auto kg = step(keyboard | gamepad);
        const auto gOnly = step(gamepad);
        const auto finalG = step(0);
        Require(kOnly.aggregateHeld && !kOnly.virtualBridgeDesired &&
                kOnly.next.effectiveEmitter == gameplay::SustainedEffectiveEmitter::KeyboardPhysical,
            "K-only Sprint must remain physical and must not synthesize virtual press");
        Require(kg.virtualBridgeDesired && kg.joiningPressSuppressionMask == gamepad,
            "G joining physical Sprint must establish the bridge but suppress the joining edge");
        Require(gOnly.virtualBridgeDesired &&
                gOnly.nonFinalReleaseSuppressionMask == keyboard,
            "K non-final release must not interrupt the virtual bridge while G remains");
        Require(finalG.finalRelease && finalG.releaseToken != 0,
            "G final release must release the bridge exactly once");

        state = {};
        const auto mouseOnly = step(mouse);
        Require(mouseOnly.next.activeSourceMask == mouse &&
                mouseOnly.next.effectiveEmitter == gameplay::SustainedEffectiveEmitter::MousePhysical &&
                !mouseOnly.virtualBridgeDesired,
            "mouse Sprint contributor must use an independent physical bit");

        const auto sameBatch = gameplay::ResolveSustainedContributor(gameplay::SustainedContributorInput{
            .activeSourceMask = static_cast<std::uint8_t>(gamepad | keyboard),
            .gamepadEventOrdinal = 22,
            .keyboardEventOrdinal = 11
        });
        Require(sameBatch.winningPressSource == SustainedContributorBit::KeyboardPhysical &&
                sameBatch.joiningPressSuppressionMask == gamepad,
            "same-batch Sprint press must prefer the earliest physical event ordinal");

        const auto keyboardOnlyProjection = gameplay::ResolveGameplayProjection(
            Kernel(),
            Resolved(),
            gameplay::GameplayPolicy{ .keyboardPhysicalSustainedActive = true },
            gameplay::GameplayProjectionFrame{},
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });
        Require(keyboardOnlyProjection.gamepadPlan.sustainedDigital.count == 1 &&
                keyboardOnlyProjection.gamepadPlan.sustainedDigital.items[0].actionId == input::actions::Sprint &&
                keyboardOnlyProjection.gamepadPlan.sustainedDigital.items[0].activeSourceMask == keyboard &&
                !keyboardOnlyProjection.gamepadPlan.sustainedDigital.items[0].virtualBridgeDesired,
            "KBM-only Sprint must publish the complete physical mask without virtual press");

        actions::ResolvedActionFrame gamepadJoin{};
        gamepadJoin.changes.push_back(actions::ActionPhaseChange{
            .actionId = std::string(input::actions::Sprint),
            .phase = actions::ActionPhase::Press,
            .timestampUs = 30
        });
        const auto joinedProjection = gameplay::ResolveGameplayProjection(
            Kernel(),
            gamepadJoin,
            gameplay::GameplayPolicy{
                .keyboardPhysicalSustainedActive = true,
                .keyboardSustainedEventOrdinal = 10 },
            keyboardOnlyProjection,
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });
        const auto& joinedSprint = joinedProjection.gamepadPlan.sustainedDigital.items[0];
        Require(joinedSprint.activeSourceMask == static_cast<std::uint8_t>(keyboard | gamepad) &&
                joinedSprint.virtualBridgeDesired &&
                joinedSprint.joiningPressSuppressionMask == gamepad,
            "gamepad joining physical Sprint must publish bridge and suppression metadata");

        gameplay::GameplayProjectionFrame released = joinedProjection;
        released.sprintDecision = none;
        const auto idleAfterRelease = gameplay::ResolveGameplayProjection(
            Kernel(), Resolved(), gameplay::GameplayPolicy{}, released,
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });
        Require(idleAfterRelease.sprintDecision.next.lastReleaseToken == none.releaseToken,
            "idle frame must preserve the consumed-once Sprint release token sequence");
        const auto resetAfterRelease = gameplay::ResolveGameplayProjection(
            Kernel(), Resolved(), gameplay::GameplayPolicy{}, released,
            gameplay::GameplayRecoveryInput{ .hardResetRequested = true, .cleanFrame = true });
        Require(resetAfterRelease.sprintDecision.next.lastReleaseToken == 0 &&
                resetAfterRelease.sprintDecision.next.activeSourceMask == 0,
            "global hard reset must clear Sprint mask, bridge, emitter, and release token");

        gameplay::GameplayProjectionFrame activeBeforeDisconnect{};
        activeBeforeDisconnect.sprintDecision.next = gameplay::SustainedContributorState{
            .activeSourceMask = static_cast<std::uint8_t>(gamepad | keyboard),
            .virtualMaterialized = true,
            .effectiveEmitter = gameplay::SustainedEffectiveEmitter::GamepadVirtualBridge,
            .lastReleaseToken = 9
        };
        const auto disconnected = gameplay::ResolveGameplayProjection(
            Kernel(), Resolved(),
            gameplay::GameplayPolicy{
                .keyboardPhysicalSustainedActive = true,
                .clearGamepadSustainedContributor = true },
            activeBeforeDisconnect,
            gameplay::GameplayRecoveryInput{ .cleanFrame = true });
        Require(disconnected.sprintDecision.next.activeSourceMask == keyboard &&
                disconnected.sprintDecision.virtualBridgeDesired &&
                disconnected.sprintDecision.nonFinalReleaseSuppressionMask == gamepad,
            "gamepad disconnect must clear only G and preserve the materialized bridge while K remains");
        const auto scopedDisconnect = gameplay::ResolveGameplayProjection(
            Kernel(), Resolved(),
            gameplay::GameplayPolicy{
                .keyboardPhysicalSustainedActive = true,
                .clearGamepadSustainedContributor = true,
                .arbitrationResetMode = gameplay::ChannelArbitrationResetMode::GamepadSource },
            activeBeforeDisconnect,
            gameplay::GameplayRecoveryInput{
                .hardResetRequested = true,
                .cleanFrame = true,
                .resetScope = gameplay::RecoveryResetScope::GamepadSource });
        Require(scopedDisconnect.sprintDecision.next.activeSourceMask == keyboard &&
                scopedDisconnect.sprintDecision.virtualBridgeDesired &&
                scopedDisconnect.sprintDecision.nonFinalReleaseSuppressionMask == gamepad &&
                !scopedDisconnect.recoveryPlan.resetNativeCommitBackend &&
                !scopedDisconnect.recoveryPlan.resetSustainedDigitalAggregator,
            "actual GamepadSource recovery must preserve K Sprint and bridge without a global backend reset");
        const auto resetWhileActive = gameplay::ResolveGameplayProjection(
            Kernel(), Resolved(), gameplay::GameplayPolicy{}, activeBeforeDisconnect,
            gameplay::GameplayRecoveryInput{ .hardResetRequested = true, .cleanFrame = true });
        Require(resetWhileActive.sprintDecision.next.activeSourceMask == 0 &&
                !resetWhileActive.sprintDecision.next.virtualMaterialized &&
                resetWhileActive.sprintDecision.next.lastReleaseToken == 0,
            "global reset while Sprint is active must clear all contributor state");
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

        const auto gamepadSource = gameplay::BuildRecoveryPlan(gameplay::GameplayRecoveryInput{
            .hardResetRequested = true,
            .cleanFrame = true,
            .resetScope = gameplay::RecoveryResetScope::GamepadSource
        });
        Require(gamepadSource.mode == gameplay::RecoveryMode::HardResetOutputs,
            "gamepad-scoped reset must remain an explicit hard recovery transaction");
        Require(!gamepadSource.resetNativeCommitBackend &&
                !gamepadSource.resetKeyboardHelperBackend &&
                !gamepadSource.resetSustainedDigitalAggregator &&
                !gamepadSource.clearProjectionStickyOwners &&
                !gamepadSource.clearRecoveryBaseline,
            "gamepad-scoped reset must not widen into global backend, helper, Sprint, or owner reset");
        const std::vector<gameplay::RecoveryExecutionStep> expectedGamepadSource{
            gameplay::RecoveryExecutionStep::ApplyOutputPlans,
            gameplay::RecoveryExecutionStep::CommitCleanRecoveryBaseline
        };
        Require(gameplay::BuildRecoveryExecutionPlan(gamepadSource) == expectedGamepadSource,
            "gamepad-scoped recovery must neutralize through scoped output plans only");
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

        gameplay::DualPadRuntime arbitrationRuntime;
        arbitrationRuntime.ResetForTests();
        gameplay::DualPadRuntimeInput mouseAndStick{
            .kernel = Kernel(),
            .resolved = ResolvedAxes(0.65f, 0.0f),
            .policy = gameplay::GameplayPolicy{
                .outputTickUs = 100'000,
                .lastPhysicalMouseMoveOwnerUs = 100'000,
                .mouseLookActive = true,
                .mouseLookActivatedThisFrame = true },
            .recovery = gameplay::GameplayRecoveryInput{ .cleanFrame = true },
            .outputTick = 100'000
        };
        RecordingPollOutputExecutor failedCandidateExecutor;
        failedCandidateExecutor.failOnAnalogPublish = true;
        const auto failedCandidate = arbitrationRuntime.ProcessGameplayFrameForTests(
            mouseAndStick,
            failedCandidateExecutor);
        Require(!failedCandidate.output.outputApplySucceeded && failedCandidate.projectionFrame.nextArbitration.look.gamepadCandidate,
            "failed output fixture must calculate but not commit an RS candidate");
        Require(gameplay::RuntimeInputPublication::GetSingleton().GetCommitted().revision == 0,
            "failed output apply must not commit current-cycle-sensitive state");

        gameplay::DualPadRuntimeInput sustainOnly{
            .kernel = Kernel(),
            .resolved = ResolvedAxes(0.16f, 0.0f),
            .policy = gameplay::GameplayPolicy{
                .outputTickUs = 301'000,
                .lastPhysicalMouseMoveOwnerUs = 100'000 },
            .recovery = gameplay::GameplayRecoveryInput{ .cleanFrame = true },
            .outputTick = 301'000
        };
        RecordingPollOutputExecutor afterFailureExecutor;
        const auto afterFailure = arbitrationRuntime.ProcessGameplayFrameForTests(
            sustainOnly,
            afterFailureExecutor);
        Require(afterFailure.projectionFrame.lookOwner == gameplay::ChannelOwner::None,
            "failed output apply must not advance channel candidate state");

        gameplay::DualPadRuntime auditFailureRuntime;
        auditFailureRuntime.ResetForTests();
        auto conflictingPlanInput = ExactCurrentCycleInput();
        conflictingPlanInput.physicalLookActivation = true;
        conflictingPlanInput.materializedLookEvent = true;
        const auto conflictingPlan = gameplay::BuildCurrentCycleGatePlan(conflictingPlanInput);
        gameplay::DualPadRuntimeInput auditFailureInput{
            .kernel = Kernel(),
            .resolved = ResolvedAxes(0.70f, 0.0f),
            .policy = gameplay::GameplayPolicy{ .outputTickUs = 400'000 },
            .recovery = gameplay::GameplayRecoveryInput{ .cleanFrame = true },
            .currentCyclePlan = conflictingPlan,
            .currentCycleAudit = gameplay::CurrentCycleAdapterAudit{
                .success = true,
                .shadowOnly = true,
                .mutationApplied = false,
                .affectedChannels = gameplay::CurrentCycleChannelMask(gameplay::CurrentCycleChannel::Look) },
            .outputTick = 400'000
        };
        RecordingPollOutputExecutor auditFailureExecutor;
        const auto auditFailure = auditFailureRuntime.ProcessGameplayFrameForTests(
            auditFailureInput,
            auditFailureExecutor);
        Require(auditFailure.output.outputApplySucceeded &&
                auditFailure.projectionFrame.lookOwner == gameplay::ChannelOwner::None &&
                auditFailure.projectionFrame.gamepadPlan.analog.lookX == 0.0f,
            "unapplied shadow mutation must publish a fail-closed affected next-Poll channel");
        Require(gameplay::RuntimeInputPublication::GetSingleton().GetCommitted().revision == 0,
            "unapplied shadow mutation must not commit runtime sensitive state");

        gameplay::DualPadRuntime sprintRollbackRuntime;
        sprintRollbackRuntime.ResetForTests();
        gameplay::DualPadRuntimeInput keyboardSprintInput{
            .kernel = Kernel(),
            .resolved = Resolved(),
            .policy = gameplay::GameplayPolicy{ .keyboardPhysicalSustainedActive = true },
            .recovery = gameplay::GameplayRecoveryInput{ .cleanFrame = true },
            .outputTick = 500'000
        };
        RecordingPollOutputExecutor keyboardSprintExecutor;
        const auto keyboardSprint = sprintRollbackRuntime.ProcessGameplayFrameForTests(
            keyboardSprintInput,
            keyboardSprintExecutor);
        const auto keyboardMask = gameplay::SustainedContributorMask(
            gameplay::SustainedContributorBit::KeyboardPhysical);
        Require(keyboardSprint.output.outputApplySucceeded &&
                gameplay::RuntimeInputPublication::GetSingleton().GetCommitted()
                    .sprint.activeSourceMask == keyboardMask,
            "K-only Sprint must commit its physical contributor without a virtual bridge");

        auto gamepadJoinResolved = Resolved();
        gamepadJoinResolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = std::string(input::actions::Sprint),
            .phase = actions::ActionPhase::Press,
            .timestampUs = 30
        });
        keyboardSprintInput.resolved = std::move(gamepadJoinResolved);
        keyboardSprintInput.policy.keyboardSustainedEventOrdinal = 10;
        keyboardSprintInput.outputTick = 501'000;
        RecordingPollOutputExecutor gamepadJoinExecutor;
        const auto gamepadJoin = sprintRollbackRuntime.ProcessGameplayFrameForTests(
            keyboardSprintInput,
            gamepadJoinExecutor);
        const auto committedAfterJoin =
            gameplay::RuntimeInputPublication::GetSingleton().GetCommitted();
        Require(gamepadJoin.output.outputApplySucceeded &&
                !gamepadJoin.projectionFrame.sprintDecision.virtualBridgeDesired &&
                gamepadJoinExecutor.lastSustained.has_value() &&
                gamepadJoinExecutor.lastSustained->activeSourceMask == keyboardMask &&
                committedAfterJoin.revision == 1 &&
                committedAfterJoin.sprint.activeSourceMask == keyboardMask &&
                !committedAfterJoin.sprint.virtualMaterialized,
            "shadow-only joining suppression must fail-close output and preserve previous Sprint ledger");
    }

    void RunCoordinatorAuthorityCutoverTests()
    {
        Require(
            !gameplay::DualPadRuntime::LiveCoordinatorPresentationAuthorityReachable(),
            "legacy gameplay ownership authority must not be reachable from live runtime");
    }

    void RunMixedInputEvidenceTests()
    {
        input::PollMaterializationReceipt receipt{
            .hookCallSequence = 41,
            .threadId = 77,
            .identity = input::PollFrameIdentity{
                .publicationGeneration = 12,
                .runtimeGeneration = 11,
                .packetNumber = 9,
                .inputStateEpoch = 4,
                .gamepadSessionId = 2,
                .contextRevision = 7,
                .controlMapRevision = 3,
                .orderedCutoffSeq = 90,
                .eventBatchToken = 33 },
            .serializeSucceeded = true,
            .routeAvailable = true,
            .materializedLookEvent = true
        };
        gameplay::CurrentCycleSensitiveState previous{};
        previous.revision = 8;
        previous.sprint.activeSourceMask = 1;
        auto after = previous;
        telemetry::MixedInputEvidenceRecord record{
            .monotonicUs = 1'000'000,
            .ownerTickToken = 100,
            .currentInputStateEpoch = 4,
            .currentGamepadSessionId = 2,
            .receipt = receipt,
            .plan = gameplay::CurrentCycleGatePlan{
                .look = gameplay::CurrentCycleEventDisposition::Neutralize,
                .affectedChannels = gameplay::CurrentCycleChannelMask(
                    gameplay::CurrentCycleChannel::Look),
                .requiresEventMutation = true,
                .currentEventWriterCount = 1,
                .nextPollWriterCount = 1 },
            .audit = gameplay::CurrentCycleAdapterAudit{
                .success = true,
                .shadowOnly = true,
                .mutationApplied = false,
                .affectedChannels = gameplay::CurrentCycleChannelMask(
                    gameplay::CurrentCycleChannel::Look),
                .wouldMutateCount = 1,
                .currentEventWriterCount = 1 },
            .before = previous,
            .after = after,
            .commit = gameplay::RuntimeInputCommitResult{
                .failClosedChannels = gameplay::CurrentCycleChannelMask(
                    gameplay::CurrentCycleChannel::Look) }
        };

        const auto json = telemetry::SerializeMixedInputEvidenceJsonLine(record);
        Require(json.find("\"schemaVersion\":1") != std::string::npos &&
                json.find("\"ownerTickToken\":100") != std::string::npos &&
                json.find("\"materializationId\":\"41:12:11:9\"") != std::string::npos &&
                json.find("\"Look\":1") != std::string::npos &&
                json.find("\"result\":\"ShadowOnly\"") != std::string::npos &&
                json.find("\"sensitiveLedgerBefore\":\"8/1/0\"") != std::string::npos &&
                json.find("\"sensitiveLedgerAfter\":\"8/1/0\"") != std::string::npos,
            "mixed-input evidence JSON must bind receipt, writers and rollback ledger");

        telemetry::MixedInputEvidenceSampler sampler;
        Require(sampler.ShouldEmit(record), "first mixed-input evidence record must emit");
        record.monotonicUs += 1'000'000;
        record.ownerTickToken++;
        Require(!sampler.ShouldEmit(record),
            "unchanged high-rate evidence must not emit before the health interval");
        record.monotonicUs += 9'000'000;
        Require(sampler.ShouldEmit(record),
            "unchanged mixed-input evidence must emit a ten-second health summary");
        record.monotonicUs += 1;
        record.audit.failure = gameplay::CurrentCycleGateFailure::AdapterFailure;
        Require(sampler.ShouldEmit(record), "evidence decision changes must emit immediately");
        record.audit.failure = gameplay::CurrentCycleGateFailure::None;

        const auto traceRoot = std::filesystem::temp_directory_path() /
            "dualpad-mixed-input-evidence";
        const auto configPath = std::filesystem::temp_directory_path() /
            "dualpad-mixed-input-evidence.ini";
        std::error_code error;
        std::filesystem::remove_all(traceRoot, error);
        {
            std::ofstream config(configPath, std::ios::trunc);
            config << "[Replay]\n"
                   << "enable_trace_recording = true\n"
                   << "trace_output_dir = " << traceRoot.string() << "\n"
                   << "trace_session = i-p-shadow\n";
        }
        Require(input::RuntimeConfig::GetSingleton().Load(configPath),
            "mixed-input evidence trace config must load");
        telemetry::MixedInputEvidenceRecorder::GetSingleton().ResetForTests();

        gameplay::DualPadRuntime runtime;
        runtime.ResetForTests();
        gameplay::DualPadRuntimeInput runtimeInput{
            .kernel = Kernel(),
            .resolved = Resolved(),
            .policy = gameplay::GameplayPolicy{},
            .recovery = gameplay::GameplayRecoveryInput{ .cleanFrame = true },
            .currentCyclePlan = record.plan,
            .currentCycleAudit = record.audit,
            .currentCycleEvidence = gameplay::CurrentCycleCallbackEvidence{
                .ownerTickToken = record.ownerTickToken,
                .monotonicUs = record.monotonicUs,
                .currentInputStateEpoch = record.currentInputStateEpoch,
                .currentGamepadSessionId = record.currentGamepadSessionId,
                .receipt = record.receipt },
            .inputStateEpoch = record.currentInputStateEpoch,
            .gamepadSessionId = record.currentGamepadSessionId,
            .outputTick = record.monotonicUs
        };
        RecordingPollOutputExecutor executor;
        (void)runtime.ProcessGameplayFrameForTests(runtimeInput, executor);
        runtimeInput.currentCycleEvidence->monotonicUs += 1'000'000;
        runtimeInput.outputTick += 1'000'000;
        (void)runtime.ProcessGameplayFrameForTests(runtimeInput, executor);

        const auto evidencePath = traceRoot / "i-p-shadow" / "mixed_input_evidence.jsonl";
        std::ifstream evidence(evidencePath);
        std::vector<std::string> lines;
        for (std::string line; std::getline(evidence, line);) {
            lines.push_back(std::move(line));
        }
        Require(lines.size() == 1 &&
                lines.front().find("\"caseId\":\"runtime-shadow\"") != std::string::npos &&
                lines.front().find("\"result\":\"ShadowOnly\"") != std::string::npos,
            "runtime must write bounded evaluator-compatible I-P shadow evidence");

        const auto blockedRoot = std::filesystem::temp_directory_path() /
            "dualpad-mixed-input-evidence-blocked";
        std::filesystem::remove_all(blockedRoot, error);
        {
            std::ofstream blocker(blockedRoot, std::ios::trunc);
            blocker << "not a directory";
        }
        {
            std::ofstream config(configPath, std::ios::trunc);
            config << "[Replay]\n"
                   << "enable_trace_recording = true\n"
                   << "trace_output_dir = " << blockedRoot.string() << "\n"
                   << "trace_session = i-p-shadow\n";
        }
        Require(input::RuntimeConfig::GetSingleton().Load(configPath),
            "blocked mixed-input evidence trace config must load");
        telemetry::MixedInputEvidenceRecorder::GetSingleton().ResetForTests();
        telemetry::MixedInputEvidenceRecorder::GetSingleton().Record(record);

        std::filesystem::remove(configPath, error);
        std::filesystem::remove_all(traceRoot, error);
        std::filesystem::remove(blockedRoot, error);
        const auto missingConfig = std::filesystem::temp_directory_path() /
            "dualpad-missing-mixed-input-evidence.ini";
        std::filesystem::remove(missingConfig, error);
        (void)input::RuntimeConfig::GetSingleton().Load(missingConfig);
        telemetry::MixedInputEvidenceRecorder::GetSingleton().ResetForTests();
    }
}

int main()
{
    try {
        RunFrozenFrameShapeTests();
        RunRecoveryPlanTests();
        RunPerChannelMixedInputArbitrationTests();
        RunPollMaterializationReceiptTests();
        RunCurrentCycleGateAndPreparedCommitTests();
        RunTransientActionGateTests();
        RunSprintContributorDecisionTests();
        RunProjectionClassificationAndGateTests();
        RunMenuContextGamepadOutputTests();
        RunGameplayActivateKeepsMinDownWindowLifecycleTests();
        RunGameplayFavoritesFailClosedByDefaultTests();
        RunGameplayFavoritesOptInPresentationHandoffTests();
        RunOverflowFailClosedTests();
        RunSoftRecoveryDoesNotClearOutputTests();
        RunPresentationPublisherTests();
        RunPollOutputAdapterExecutionTests();
        RunDualPadRuntimePublisherSeamTests();
        RunCoordinatorAuthorityCutoverTests();
        RunMixedInputEvidenceTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
