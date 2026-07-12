#include "pch.h"

#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/gameplay/EngineModeProjection.h"
#include "input_v2/presentation/PresentationProjection.h"
#include "input_v2/presentation/CursorHandoffAckMailbox.h"
#include "input_v2/presentation/CursorHandoffCoordinator.h"
#include "input_v2/presentation/GameplayPresentationAdapter.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/presentation/SkyrimEngineModeRouter.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"
#include "input_v2/runtime/EngineModeDecision.h"
#include "input/SkyrimCursorHandoffAdapter.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    dualpad::input_v2::context::ResolvedContextSnapshot GameplayContext()
    {
        dualpad::input_v2::context::ResolvedContextSnapshot context{};
        context.hostMode = dualpad::input_v2::context::HostMode::Gameplay;
        context.uiContextId = dualpad::input_v2::context::UiContextId::None;
        context.actionSetStack.baseSetId = "GameplayBase";
        context.presentationPolicyId = "GameplayPolicyFromPH2";
        context.contextRevision = 7;
        return context;
    }

    dualpad::input_v2::context::ResolvedContextSnapshot MenuContext()
    {
        auto context = GameplayContext();
        context.hostMode = dualpad::input_v2::context::HostMode::Menu;
        context.uiContextId = dualpad::input_v2::context::UiContextId::Journal;
        context.actionSetStack.baseSetId = "MenuBase";
        context.actionSetStack.layerIds = { "JournalLayer" };
        context.actionSetStack.scopeAnchorIds = { "MenuBase", "JournalLayer" };
        context.presentationPolicyId = "PolicyOnlyPH2MayChoose";
        context.contextRevision = 8;
        context.menuStackRevision = 4;
        context.topMenuInstanceId = 1;
        context.topMenuName = "Journal Menu";
        context.topMenuPtr = 0x1000;
        context.topMenuMoviePtr = 0x2000;
        return context;
    }

    dualpad::input_v2::presentation::PublishedPresentationState EligibleMenuPresentation(
        std::uint32_t epoch,
        std::uint32_t contextRevision,
        dualpad::input_v2::context::UiContextId uiContextId =
            dualpad::input_v2::context::UiContextId::Journal)
    {
        dualpad::input_v2::presentation::PublishedPresentationState state{};
        state.owner = dualpad::input_v2::presentation::PresentationOwner::Gamepad;
        state.navigationOwner = dualpad::input_v2::presentation::NavigationOwner::Gamepad;
        state.cursorOwner = dualpad::input_v2::presentation::CursorOwner::Gamepad;
        state.uiContextId = uiContextId;
        state.menuRefreshEligibility =
            dualpad::input_v2::presentation::MenuRefreshEligibility::EligibleStableMenu;
        state.targetMenuName = "Journal Menu";
        state.targetMenuInstanceId = 1;
        state.targetMenuPtr = 0x1000;
        state.targetMenuMoviePtr = 0x2000;
        state.menuStackRevision = contextRevision;
        state.contextRevision = contextRevision;
        state.actionSetStack.baseSetId = "MenuBase";
        state.actionSetStack.layerIds = { "MenuLayer" };
        state.actionSetStack.scopeAnchorIds = { "MenuBase", "MenuLayer" };
        state.presentationPolicyId = "Menu";
        state.epoch = epoch;
        state.dirty = dualpad::input_v2::presentation::PresentationDirtyFlags::Context;
        return state;
    }

    dualpad::input_v2::ingress::MeaningfulSourceActivity Activity(
        dualpad::input_v2::ingress::PhysicalInputSource source,
        dualpad::input_v2::ingress::SourceActivityKind kind,
        std::uint64_t seq,
        std::uint64_t producerTimestampUs,
        std::int32_t deltaX = 0,
        std::int32_t deltaY = 0)
    {
        return dualpad::input_v2::ingress::MeaningfulSourceActivity{
            { source, kind, 0, deltaX, deltaY, producerTimestampUs },
            seq,
            7,
            source == dualpad::input_v2::ingress::PhysicalInputSource::Gamepad ? 3u : 0u,
            8
        };
    }
}

void RunOrderedActivityRoutingAndIndependentProjectionTests()
{
    namespace input_v2 = dualpad::input_v2;
    namespace ingress = input_v2::ingress;
    namespace presentation = input_v2::presentation;

    const auto context = MenuContext();
    const input_v2::actions::ResolvedActionFrame resolved{};
    const std::vector<ingress::MeaningfulSourceActivity> keyboardWins{
        Activity(ingress::PhysicalInputSource::Gamepad,
            ingress::SourceActivityKind::GamepadButtonPress, 50, 9'000),
        Activity(ingress::PhysicalInputSource::Keyboard,
            ingress::SourceActivityKind::KeyboardPress, 51, 1)
    };
    const auto routedKeyboard = ingress::RouteSourceActivities(
        keyboardWins, {}, context, resolved, 1000);
    presentation::PublishedPresentationState initial{};
    const auto keyboardDecision = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = initial,
            .context = context,
            .gameplayMenuEntryOwner = presentation::PresentationOwner::Gamepad,
            .routedActivities = routedKeyboard.activities,
            .inputStateEpoch = 7,
            .ownerTickToken = 10
        });
    Require(keyboardDecision.state.prompt.family == presentation::DeviceFamily::KeyboardMouse &&
            keyboardDecision.state.prompt.acceptedActivitySeq == 51 &&
            keyboardDecision.state.menu.owner == presentation::PresentationOwner::KeyboardMouse,
        "higher ingress seq keyboard activity must win prompt/menu regardless of producer timestamp");

    const std::vector<ingress::MeaningfulSourceActivity> gamepadWins{
        Activity(ingress::PhysicalInputSource::Keyboard,
            ingress::SourceActivityKind::KeyboardPress, 50, 99'000),
        Activity(ingress::PhysicalInputSource::Gamepad,
            ingress::SourceActivityKind::GamepadButtonPress, 51, 1)
    };
    const auto routedGamepad = ingress::RouteSourceActivities(
        gamepadWins, {}, context, resolved, 1000);
    const auto gamepadDecision = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = initial,
            .context = context,
            .routedActivities = routedGamepad.activities,
            .inputStateEpoch = 7,
            .ownerTickToken = 11
        });
    Require(gamepadDecision.state.prompt.family == presentation::DeviceFamily::Gamepad &&
            gamepadDecision.state.prompt.acceptedActivitySeq == 51 &&
            gamepadDecision.state.menu.owner == presentation::PresentationOwner::Gamepad,
        "higher ingress seq gamepad activity must win prompt/menu regardless of producer timestamp");

    const auto routedSameGamepad = ingress::RouteSourceActivities(
        std::vector<ingress::MeaningfulSourceActivity>{ Activity(
            ingress::PhysicalInputSource::Gamepad,
            ingress::SourceActivityKind::GamepadButtonPress,
            52,
            2) },
        routedGamepad.next,
        context,
        resolved,
        1001);
    const auto sameGamepadDecision = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = gamepadDecision.state,
            .context = context,
            .routedActivities = routedSameGamepad.activities,
            .inputStateEpoch = 8,
            .ownerTickToken = 12
        });
    Require(sameGamepadDecision.state.prompt.acceptedActivitySeq == 52 &&
            sameGamepadDecision.state.menu.acceptedActivitySeq == 52 &&
            sameGamepadDecision.state.cursor.acceptedActivitySeq == 52,
        "same-owner activity must still advance consume-once presentation ledgers");
    Require(sameGamepadDecision.state.dirty == presentation::PresentationDirtyFlags::None &&
            sameGamepadDecision.state.presentationEpoch ==
                gamepadDecision.state.presentationEpoch &&
            !sameGamepadDecision.refreshTargetMenu,
        "same-owner activity must not manufacture family/owner/cursor dirty or refresh the menu");

    const auto neutralDecision = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = gamepadDecision.state,
            .context = context,
            .routedActivities = {},
            .inputStateEpoch = 7,
            .ownerTickToken = 11
        });
    Require(neutralDecision.state.prompt == gamepadDecision.state.prompt &&
            neutralDecision.state.menu == gamepadDecision.state.menu &&
            neutralDecision.state.cursor == gamepadDecision.state.cursor,
        "neutral/release-only/unchanged/suppressed cycles must carry all presentation decisions");

    const auto ninePixelCandidate = ingress::RouteSourceActivities(
        std::vector<ingress::MeaningfulSourceActivity>{ Activity(
            ingress::PhysicalInputSource::Mouse,
            ingress::SourceActivityKind::MouseDelta,
            59,
            9,
            9,
            0) },
        {}, context, resolved, 5000);
    const auto nineAfterDeadline = ingress::RouteSourceActivities(
        {}, ninePixelCandidate.next, context, resolved, 5120);
    Require(nineAfterDeadline.activities.empty(),
        "9 px pointer movement must not promote even after the owner deadline");

    const auto pointerStart = ingress::RouteSourceActivities(
        std::vector<ingress::MeaningfulSourceActivity>{ Activity(
            ingress::PhysicalInputSource::Mouse,
            ingress::SourceActivityKind::MouseDelta,
            60,
            10,
            6,
            0) },
        {}, context, resolved, 1000);
    const auto pointerThreshold = ingress::RouteSourceActivities(
        std::vector<ingress::MeaningfulSourceActivity>{ Activity(
            ingress::PhysicalInputSource::Mouse,
            ingress::SourceActivityKind::MouseDelta,
            61,
            11,
            4,
            0) },
        pointerStart.next, context, resolved, 1050);
    const auto beforeDeadline = ingress::RouteSourceActivities(
        {}, pointerThreshold.next, context, resolved, 1119);
    Require(beforeDeadline.activities.empty(),
        "10 px pointer candidate must not promote before the 120 ms owner deadline");
    const auto atDeadline = ingress::RouteSourceActivities(
        {}, beforeDeadline.next, context, resolved, 1120);
    Require(atDeadline.activities.size() == 1 &&
            atDeadline.activities.front().qualifiedByOwnerTimer &&
            atDeadline.activities.front().qualifiesForPrompt &&
            atDeadline.activities.front().qualifiesForCursor &&
            !atDeadline.activities.front().strongForMenuOwner,
        "owner tick must promote a stopped 10 px pointer candidate without another mouse event");

    auto pointerPrevious = gamepadDecision.state;
    pointerPrevious.cursor.committedOwner = presentation::CursorOwner::Gamepad;
    pointerPrevious.cursor.requestedOwner = presentation::CursorOwner::Gamepad;
    pointerPrevious.cursor.pendingToken = 0;
    const auto pointerDecision = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = pointerPrevious,
            .context = context,
            .routedActivities = atDeadline.activities,
            .inputStateEpoch = 7,
            .ownerTickToken = 12,
            .gamepadToKeyboardMouseSync = presentation::CursorPositionSyncPolicy::NotRequired
        });
    Require(pointerDecision.state.prompt.family == presentation::DeviceFamily::KeyboardMouse &&
            pointerDecision.state.menu.owner == presentation::PresentationOwner::Gamepad &&
            pointerDecision.state.cursor.committedOwner == presentation::CursorOwner::KeyboardMouse,
        "pointer promotion must change prompt/cursor without changing menu navigation owner");

    const auto candidateAgain = ingress::RouteSourceActivities(
        std::vector<ingress::MeaningfulSourceActivity>{ Activity(
            ingress::PhysicalInputSource::Mouse,
            ingress::SourceActivityKind::MouseDelta,
            70,
            20,
            10,
            0) },
        {}, context, resolved, 2000);
    const auto competed = ingress::RouteSourceActivities(
        std::vector<ingress::MeaningfulSourceActivity>{ Activity(
            ingress::PhysicalInputSource::Gamepad,
            ingress::SourceActivityKind::GamepadButtonPress,
            71,
            1) },
        candidateAgain.next, context, resolved, 2050);
    const auto staleTimer = ingress::RouteSourceActivities(
        {}, competed.next, context, resolved, 2120);
    Require(staleTimer.activities.empty(),
        "higher-seq strong gamepad activity must cancel an older pointer timer candidate");

    const auto mouseClick = ingress::RouteSourceActivities(
        std::vector<ingress::MeaningfulSourceActivity>{ Activity(
            ingress::PhysicalInputSource::Mouse,
            ingress::SourceActivityKind::MouseButtonPress,
            80,
            30) },
        {}, context, resolved, 3000);
    const auto clickDecision = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = pointerPrevious,
            .context = context,
            .routedActivities = mouseClick.activities,
            .inputStateEpoch = 7,
            .ownerTickToken = 13,
            .gamepadToKeyboardMouseSync = presentation::CursorPositionSyncPolicy::NotRequired
        });
    Require(clickDecision.state.menu.owner == presentation::PresentationOwner::KeyboardMouse &&
            clickDecision.state.menu.navigationOwner == presentation::NavigationOwner::KeyboardMouse &&
            clickDecision.state.cursor.committedOwner == presentation::CursorOwner::KeyboardMouse,
        "mouse click must strongly switch menu navigation and cursor to KBM");

    const auto promptOnly = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = pointerPrevious,
            .context = context,
            .routedActivities = atDeadline.activities,
            .inputStateEpoch = 7,
            .ownerTickToken = 14,
            .gamepadToKeyboardMouseSync = presentation::CursorPositionSyncPolicy::MappingUnverified
        });
    Require(promptOnly.state.prompt.family == presentation::DeviceFamily::KeyboardMouse &&
            promptOnly.state.menu == pointerPrevious.menu &&
            promptOnly.state.cursor.committedOwner == pointerPrevious.cursor.committedOwner,
        "prompt-only activity must not mutate menu or committed cursor state");
}

void RunCursorPlanAckShadowTests()
{
    namespace input_v2 = dualpad::input_v2;
    namespace ingress = input_v2::ingress;
    namespace presentation = input_v2::presentation;

    const auto context = MenuContext();
    const input_v2::actions::ResolvedActionFrame resolved{};
    const auto routed = ingress::RouteSourceActivities(
        std::vector<ingress::MeaningfulSourceActivity>{ Activity(
            ingress::PhysicalInputSource::Gamepad,
            ingress::SourceActivityKind::GamepadButtonPress,
            90,
            1) },
        {}, context, resolved, 4000);
    presentation::PublishedPresentationState previous{};
    const auto pending = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = previous,
            .context = context,
            .routedActivities = routed.activities,
            .inputStateEpoch = 7,
            .ownerTickToken = 900,
            .keyboardMouseToGamepadSync = presentation::CursorPositionSyncPolicy::Required
        });
    Require(pending.cursorPlan.has_value() &&
            pending.state.cursor.requestedOwner == presentation::CursorOwner::Gamepad &&
            pending.state.cursor.committedOwner == presentation::CursorOwner::KeyboardMouse &&
            pending.state.cursor.reason == presentation::CursorOwnerDecisionReason::HandoffPending,
        "KBM-to-gamepad cursor request must remain pending until exact success ack");

    dualpad::input::SkyrimCursorHandoffAdapter adapter;
    const auto shadowAck = adapter.ExecuteVerifiedHandoff(*pending.cursorPlan);
    Require(shadowAck.failure == presentation::CursorHandoffFailure::MappingUnverified &&
            !shadowAck.positionSynchronized,
        "I-CURSOR shadow adapter must fail MappingUnverified without coordinate side effects");
    const auto shadowRejected = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = pending.state,
            .context = context,
            .cursorAck = shadowAck,
            .inputStateEpoch = 7,
            .ownerTickToken = 901,
            .keyboardMouseToGamepadSync = presentation::CursorPositionSyncPolicy::Required
        });
    Require(shadowRejected.state.cursor.committedOwner == presentation::CursorOwner::KeyboardMouse,
        "MappingUnverified ack must never commit cursor owner");

    presentation::CursorHandoffAckMailbox mailbox;
    auto successAck = shadowAck;
    successAck.failure = presentation::CursorHandoffFailure::None;
    successAck.positionSynchronized = true;
    mailbox.PublishFromUiTask(successAck);
    const auto consumed = mailbox.ConsumeExactOnOwnerTick(
        pending.cursorPlan->token,
        pending.cursorPlan->contextRevision,
        pending.cursorPlan->presentationEpoch,
        pending.cursorPlan->targetMenuInstanceId);
    Require(consumed.has_value(), "exact cursor ack envelope must be consumed once");
    Require(!mailbox.ConsumeExactOnOwnerTick(
                pending.cursorPlan->token,
                pending.cursorPlan->contextRevision,
                pending.cursorPlan->presentationEpoch,
                pending.cursorPlan->targetMenuInstanceId)
                .has_value(),
        "duplicate cursor ack consume must fail closed");

    const auto committed = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = pending.state,
            .context = context,
            .cursorAck = consumed->ack,
            .inputStateEpoch = 7,
            .ownerTickToken = 902,
            .keyboardMouseToGamepadSync = presentation::CursorPositionSyncPolicy::Required
        });
    Require(committed.state.cursor.committedOwner == presentation::CursorOwner::Gamepad &&
            committed.state.cursor.pendingToken == 0 &&
            committed.state.cursor.reason == presentation::CursorOwnerDecisionReason::HandoffCommitted,
        "exact success ack must commit cursor owner and clear pending token");

    auto wrongToken = successAck;
    ++wrongToken.token;
    const auto wrongRejected = presentation::ProjectPresentation(
        presentation::PresentationProjectionInput{
            .previous = pending.state,
            .context = context,
            .cursorAck = wrongToken,
            .inputStateEpoch = 7,
            .ownerTickToken = 903,
            .keyboardMouseToGamepadSync = presentation::CursorPositionSyncPolicy::Required
        });
    Require(wrongRejected.state.cursor.committedOwner == presentation::CursorOwner::KeyboardMouse,
        "wrong-token cursor ack must not commit");

    const auto requireRejectedAck = [&](presentation::CursorHandoffAck ack, const char* message) {
        const auto rejected = presentation::ProjectPresentation(
            presentation::PresentationProjectionInput{
                .previous = pending.state,
                .context = context,
                .cursorAck = ack,
                .inputStateEpoch = 7,
                .ownerTickToken = 904,
                .keyboardMouseToGamepadSync = presentation::CursorPositionSyncPolicy::Required
            });
        Require(rejected.state.cursor.committedOwner == presentation::CursorOwner::KeyboardMouse &&
                rejected.state.cursor.pendingToken == pending.cursorPlan->token,
            message);
    };

    auto wrongContext = successAck;
    ++wrongContext.contextRevision;
    requireRejectedAck(wrongContext, "wrong-context cursor ack must fail closed");

    auto wrongEpoch = successAck;
    ++wrongEpoch.presentationEpoch;
    requireRejectedAck(wrongEpoch, "wrong-epoch cursor ack must fail closed");

    auto wrongInstance = successAck;
    ++wrongInstance.targetMenuInstanceId;
    requireRejectedAck(wrongInstance, "wrong-menu-instance cursor ack must fail closed");

    auto writeFailure = successAck;
    writeFailure.failure = presentation::CursorHandoffFailure::WriteVerificationFailed;
    writeFailure.positionSynchronized = false;
    requireRejectedAck(writeFailure, "failed cursor coordinate verification must fail closed");

    presentation::SkyrimCompatibilitySurface compat;
    auto committedState = EligibleMenuPresentation(5, 8);
    compat.Commit(committedState);
    compat.CommitPreOutputGameplayPresentationHandoff(presentation::PresentationOwner::Gamepad);
    const auto intentOnly = compat.GetCommittedState();
    Require(intentOnly.owner == committedState.owner &&
            intentOnly.navigationOwner == committedState.navigationOwner &&
            intentOnly.cursorOwner == committedState.cursorOwner &&
            intentOnly.epoch == committedState.epoch &&
            intentOnly.gameplayMenuEntryIntentOwner == presentation::PresentationOwner::Gamepad,
        "gameplay pre-output handoff must publish menu-entry intent without forcing menu/cursor owner");

    presentation::PresentationProjection liveProjection;
    const presentation::SourceEvidenceSnapshot evidence{};
    const presentation::PublishedGameplayPresentation gameplay{};
    const std::vector<ingress::MeaningfulSourceActivity> gamepadActivity{
        Activity(
            ingress::PhysicalInputSource::Gamepad,
            ingress::SourceActivityKind::GamepadButtonPress,
            100,
            1)
    };
    const auto gamepadPending = liveProjection.ProjectOrdered(
        evidence,
        context,
        gameplay,
        gamepadActivity,
        resolved,
        5'000,
        7,
        1'000);
    const auto gamepadPlan = liveProjection.GetPendingCursorPlan();
    Require(gamepadPlan.has_value() &&
            gamepadPending.cursor.committedOwner == presentation::CursorOwner::KeyboardMouse,
        "live projection must first keep KBM cursor until exact gamepad handoff ack");

    const presentation::CursorHandoffAck gamepadAck{
        .token = gamepadPlan->token,
        .targetMenuInstanceId = gamepadPlan->targetMenuInstanceId,
        .contextRevision = gamepadPlan->contextRevision,
        .presentationEpoch = gamepadPlan->presentationEpoch,
        .positionSynchronized = true,
        .failure = presentation::CursorHandoffFailure::None
    };
    const auto gamepadCommitted = liveProjection.ProjectOrdered(
        evidence,
        context,
        gameplay,
        {},
        resolved,
        5'001,
        7,
        1'001,
        gamepadAck);
    Require(gamepadCommitted.cursor.committedOwner == presentation::CursorOwner::Gamepad,
        "exact test ack must establish a gamepad cursor baseline");

    const std::vector<ingress::MeaningfulSourceActivity> mouseActivity{
        Activity(
            ingress::PhysicalInputSource::Mouse,
            ingress::SourceActivityKind::MouseButtonPress,
            101,
            2)
    };
    const auto mousePending = liveProjection.ProjectOrdered(
        evidence,
        context,
        gameplay,
        mouseActivity,
        resolved,
        5'002,
        7,
        1'002);
    Require(mousePending.cursor.requestedOwner == presentation::CursorOwner::KeyboardMouse &&
            mousePending.cursor.committedOwner == presentation::CursorOwner::Gamepad &&
            mousePending.cursor.pendingToken != 0 &&
            liveProjection.GetPendingCursorPlan().has_value(),
        "I-CURSOR NO-GO must keep gamepad-to-KBM handoff pending until exact ack or a proven no-sync verdict");
}

void RunEngineHookIdentityTests()
{
    namespace presentation = dualpad::input_v2::presentation;

    constexpr std::uintptr_t moduleBase = 0x140000000;
    constexpr std::uintptr_t queryRva = 0xC15240;
    constexpr std::uintptr_t handlerColRva = 0x175E848;
    constexpr std::uintptr_t handlerVtableRva = 0x175E850;
    constexpr std::uintptr_t handlerColTargetRva = 0x194DA20;
    constexpr std::uintptr_t approvedTarget = 0x140C1A000;
    std::array<std::uint8_t, 32> entryBytes{};
    for (std::size_t index = 0; index < entryBytes.size(); ++index) {
        entryBytes[index] = static_cast<std::uint8_t>(0x40 + index);
    }

    const presentation::EngineHookIdentityManifest manifest{
        .expectedQueryRva = queryRva,
        .expectedQueryBytes = entryBytes,
        .handlerCompleteObjectLocatorRelocationId = 560029,
        .handlerVtableRelocationId = 285457,
        .expectedHandlerCompleteObjectLocatorTargetRva = handlerColTargetRva,
        .approvedDeviceVfuncSlot = 7,
        .approvedDeviceVfuncTarget = approvedTarget,
        .i0Approved = true
    };
    presentation::EngineHookIdentityObservation observed{
        .handlerCompleteObjectLocatorRelocationId = 560029,
        .handlerVtableRelocationId = 285457,
        .moduleBase = moduleBase,
        .resolvedQueryAddress = moduleBase + queryRva,
        .queryBytes = entryBytes,
        .resolvedHandlerCompleteObjectLocatorAddress = moduleBase + handlerColRva,
        .handlerCompleteObjectLocatorTarget = moduleBase + handlerColTargetRva,
        .resolvedHandlerVtableAddress = moduleBase + handlerVtableRva,
        .runtimeGamepadDeviceAddress = 0x200000000,
        .runtimeGamepadDeviceVtableAddress = moduleBase + handlerVtableRva
    };
    observed.handlerVtableTargets[0] = 0x140C19000;
    observed.handlerVtableTargets[7] = approvedTarget;
    observed.handlerVtableTargets[8] = 0x140C1B000;
    observed.handlerVtableTargets[9] = 0x140C1C000;

    Require(presentation::kGamepadHandlerVtableRelocationId == 285457,
        "I-0 must resolve the CommonLib BSPCGamepadDeviceHandler vftable ID, not the adjacent COL ID");
    Require(presentation::kGamepadHandlerCompleteObjectLocatorRelocationId == 560029,
        "I-0 must retain REL 560029 as the adjacent Complete Object Locator entry identity");

    const auto verified = presentation::VerifyEngineHookIdentity(manifest, observed);
    Require(verified.status == presentation::EngineHookIdentityStatus::Verified &&
            verified.deviceVfuncSlot == 7,
        "REL 67320 bytes and exactly one I-0 recorded handler slot must verify together");

    auto wrongAddress = observed;
    ++wrongAddress.resolvedQueryAddress;
    Require(
        presentation::VerifyEngineHookIdentity(manifest, wrongAddress).status ==
            presentation::EngineHookIdentityStatus::QueryRvaMismatch,
        "REL 67320 resolved VA mismatch must stop before patching");

    auto wrongBytes = observed;
    wrongBytes.queryBytes[31] ^= 0xFF;
    Require(
        presentation::VerifyEngineHookIdentity(manifest, wrongBytes).status ==
            presentation::EngineHookIdentityStatus::QueryBytesMismatch,
        "all 32 I-0 recorded query bytes must match exactly");

    auto missingRuntimeDevice = observed;
    missingRuntimeDevice.runtimeGamepadDeviceAddress = 0;
    missingRuntimeDevice.runtimeGamepadDeviceVtableAddress = 0;
    Require(
        presentation::VerifyEngineHookIdentity(manifest, missingRuntimeDevice).status ==
            presentation::EngineHookIdentityStatus::RuntimeDeviceMissing,
        "I-0 must fail closed when devices[kGamepad] is unavailable");

    auto mismatchedRuntimeVtable = observed;
    mismatchedRuntimeVtable.runtimeGamepadDeviceVtableAddress = 0x140F01000;
    Require(
        presentation::VerifyEngineHookIdentity(manifest, mismatchedRuntimeVtable).status ==
            presentation::EngineHookIdentityStatus::RuntimeHandlerVtableMismatch,
        "I-0 must prove the live devices[kGamepad] object uses the handler vftable relocation");

    auto mismatchedHandlerCol = observed;
    mismatchedHandlerCol.handlerCompleteObjectLocatorTarget += sizeof(std::uintptr_t);
    Require(
        presentation::VerifyEngineHookIdentity(manifest, mismatchedHandlerCol).status ==
            presentation::EngineHookIdentityStatus::HandlerCompleteObjectLocatorMismatch,
        "I-0 must prove REL 560029 is the handler COL entry adjacent to the actual vftable");

    auto missingHandlerCol = observed;
    missingHandlerCol.resolvedHandlerCompleteObjectLocatorAddress = 0;
    missingHandlerCol.handlerCompleteObjectLocatorTarget = 0;
    Require(
        presentation::VerifyEngineHookIdentity(manifest, missingHandlerCol).status ==
            presentation::EngineHookIdentityStatus::HandlerCompleteObjectLocatorMissing,
        "I-0 must fail closed when the handler COL entry cannot be observed");

    auto wrongHandlerColId = observed;
    ++wrongHandlerColId.handlerCompleteObjectLocatorRelocationId;
    Require(
        presentation::VerifyEngineHookIdentity(manifest, wrongHandlerColId).status ==
            presentation::EngineHookIdentityStatus::RelocationIdMismatch,
        "I-0 must reject a COL observation from any relocation other than REL 560029");
    Require(
        !presentation::BuildEngineHookIdentityProbe(
            manifest,
            wrongHandlerColId).handlerCompleteObjectLocatorMatched,
        "the read-only probe must not report COL identity matched when its relocation ID drifts");

    auto swappedSlot = observed;
    swappedSlot.handlerVtableTargets[7] = 0x140C1B000;
    swappedSlot.handlerVtableTargets[8] = approvedTarget;
    Require(
        presentation::VerifyEngineHookIdentity(manifest, swappedSlot).status ==
            presentation::EngineHookIdentityStatus::DeviceSlotMismatch,
        "I-0 static proof must reject an approved target moved from IsEnabled slot 7 to slot 8");

    auto ambiguousSlots = observed;
    ambiguousSlots.handlerVtableTargets[8] = approvedTarget;
    Require(
        presentation::VerifyEngineHookIdentity(manifest, ambiguousSlots).status ==
            presentation::EngineHookIdentityStatus::DeviceTargetAmbiguous,
        "matching both handler slots must fail closed as ambiguous");

    auto wrongApprovedSlot = manifest;
    wrongApprovedSlot.approvedDeviceVfuncSlot = 8;
    Require(
        presentation::VerifyEngineHookIdentity(wrongApprovedSlot, observed).status ==
            presentation::EngineHookIdentityStatus::DeviceSlotMismatch,
        "I-0 approval must pin IsEnabled to the statically proven slot 7");

    auto unapproved = manifest;
    unapproved.i0Approved = false;
    Require(
        presentation::VerifyEngineHookIdentity(unapproved, observed).status ==
            presentation::EngineHookIdentityStatus::GateNotApproved,
        "synthetic host identity cannot enable production without I-0 approval");

    const auto pendingProbe = presentation::BuildEngineHookIdentityProbe(
        unapproved,
        observed);
    Require(pendingProbe.staticQueryIdentityMatched &&
            pendingProbe.runtimeGamepadDevicePresent &&
            pendingProbe.runtimeHandlerVtableMatched &&
            !pendingProbe.patchEligible &&
            pendingProbe.verificationStatus ==
                presentation::EngineHookIdentityStatus::GateNotApproved &&
            pendingProbe.queryRva == queryRva &&
            pendingProbe.handlerCompleteObjectLocatorRva == handlerColRva &&
            pendingProbe.handlerCompleteObjectLocatorTargetRva == handlerColTargetRva &&
            pendingProbe.handlerVtableRva == handlerVtableRva &&
            pendingProbe.handlerVfuncTargetRvas[7] == 0xC1A000 &&
            pendingProbe.handlerVfuncTargetRvas[8] == 0xC1B000,
        "unapproved I-0 probe must capture ASLR-independent identity without enabling patch writes");
    const auto pendingDebug = presentation::ToDebugString(pendingProbe);
    Require(pendingDebug.find("patchEligible=false") != std::string::npos &&
            pendingDebug.find("runtimeDevicePresent=true") != std::string::npos &&
            pendingDebug.find("runtimeVtableMatched=true") != std::string::npos &&
            pendingDebug.find("handlerColMatched=true") != std::string::npos &&
            pendingDebug.find("queryRva=0xC15240") != std::string::npos &&
            pendingDebug.find("handlerColRva=0x175E848") != std::string::npos &&
            pendingDebug.find("handlerVtableRva=0x175E850") != std::string::npos &&
            pendingDebug.find("slot0TargetRva=0xC19000") != std::string::npos &&
            pendingDebug.find("slot7TargetRva=0xC1A000") != std::string::npos &&
            pendingDebug.find("slot8TargetRva=0xC1B000") != std::string::npos &&
            pendingDebug.find("slot9TargetRva=0xC1C000") != std::string::npos,
        "I-0 probe log must expose the exact runtime identities needed for a PASS/FAIL verdict");

    const auto production = presentation::ProductionEngineHookIdentityManifest();
    constexpr std::array<std::uint8_t, 32> approvedStaticEntryBytes{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x49, 0x70,
        0x48, 0x85, 0xC9, 0x74, 0x11, 0x48, 0x8B, 0x01,
        0xFF, 0x50, 0x38, 0x84, 0xC0, 0x74, 0x07, 0xB0,
        0x01, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x32, 0xC0
    };
    Require(!production.i0Approved &&
            production.expectedQueryRva == queryRva &&
            production.expectedQueryBytes == approvedStaticEntryBytes &&
            production.expectedHandlerCompleteObjectLocatorTargetRva ==
                handlerColTargetRva &&
            production.approvedDeviceVfuncSlot == 7 &&
            production.approvedDeviceVfuncTarget == 0,
        "matching static query bytes may seed the read-only probe but cannot pre-approve the runtime target");
}

void RunEngineOriginalFirstTests()
{
    namespace presentation = dualpad::input_v2::presentation;
    namespace runtime = dualpad::input_v2::runtime;

    presentation::SkyrimCompatibilitySurface compat;
    auto projected = EligibleMenuPresentation(20, 30);
    projected.owner = presentation::PresentationOwner::Gamepad;
    projected.cursorOwner = presentation::CursorOwner::Gamepad;
    compat.Commit(projected);
    compat.ForceOriginalHookOutputsForTests(presentation::LegacyCompatibilitySurface{
        .isUsingGamepad = false,
        .gamepadControlsCursor = false,
        .gamepadDeviceEnabled = false
    });
    compat.ForceHooksEnabledForTests(true);
    Require(!compat.IsUsingGamepadHook(),
        "unscoped engine query must return original even when presentation owner is gamepad");
    Require(!compat.GamepadControlsCursorHook(),
        "unverified cursor bool surface must remain original before I-CURSOR");
    Require(!compat.IsGamepadDeviceEnabledHook(false),
        "Native device availability must not replace original with fixed true");

    std::size_t originalCalls = 0;
    presentation::SkyrimEngineModeRouter router;
    const auto routed = router.DecideWithOriginal([&]() {
        ++originalCalls;
        return true;
    });
    Require(routed && originalCalls == 1,
        "unscoped engine gateway must call original exactly once");

    const runtime::GamepadDeviceAvailabilityDecision native{
        .policy = runtime::GamepadDeviceAvailabilityPolicy::Native,
        .allowedDomain = runtime::GamepadAvailabilityDomain::None,
        .connected = true,
        .delegateReady = true,
        .gamepadSessionId = 4,
        .contextRevision = 9
    };
    Require(!runtime::ResolveGamepadDeviceAvailability(
                native,
                runtime::GamepadAvailabilityDomain::VerifiedPollOrInitialization,
                false),
        "connectivity must not override original while availability policy is Native");
    Require(runtime::ResolveGamepadDeviceAvailability(
                native,
                runtime::GamepadAvailabilityDomain::Remap,
                true),
        "remap must preserve original under Native availability");
}

void RunEngineShadowRouterTests()
{
    namespace gameplay = dualpad::input_v2::gameplay;
    namespace presentation = dualpad::input_v2::presentation;

    const auto snapshot = gameplay::ProjectOriginalEngineModes(
        44,
        7,
        9,
        12);
    Require(snapshot.generation == 12,
        "engine shadow snapshot must carry the committed runtime generation");
    for (std::size_t index = 0; index < snapshot.byDomain.size(); ++index) {
        Require(snapshot.byDomain[index].domain == static_cast<gameplay::EngineQueryDomain>(index) &&
                snapshot.byDomain[index].mode == gameplay::EngineInputMode::Original &&
                snapshot.byDomain[index].causality == gameplay::EngineDecisionCausality::OriginalOnly,
            "WP9 shadow projection must default every engine domain to Original/OriginalOnly");
    }

    presentation::EngineCallerShadowManifest classified{};
    for (std::size_t index = 0; index < classified.rules.size(); ++index) {
        classified.rules[index] = gameplay::EngineCallerRule{
            .callerRva = 0x1000 + index * 0x10,
            .domain = gameplay::EngineQueryDomain::Unknown,
            .causality = gameplay::EngineDecisionCausality::OriginalOnly,
            .overrideEnabled = false
        };
    }
    classified.populatedRuleCount = classified.rules.size();
    classified.releaseRelevantUnknownCount = 0;
    classified.i1Approved = true;
    Require(presentation::CallerShadowTableReleaseReady(classified),
        "26 classified or OriginalOnly caller records must satisfy the host shadow table fixture");

    const auto productionManifest = presentation::ProductionEngineCallerShadowManifest();
    Require(!presentation::CallerShadowTableReleaseReady(productionManifest) &&
            productionManifest.releaseRelevantUnknownCount == 26,
        "production caller overrides must remain NO-GO until I-1 classifies all 26 xrefs");

    presentation::SkyrimEngineModeRouter router;
    Require(!router.DecideForSnapshot(0xDEADBEEF, snapshot, false),
        "unknown caller must return original");

    const auto remapSnapshot = gameplay::ProjectEngineModeShadow(
        gameplay::EngineModeProjectionInput{
            .ownerTickToken = 44,
            .inputStateEpoch = 7,
            .contextRevision = 9,
            .runtimeGeneration = 12,
            .recommendations = { gameplay::EngineModeRecommendation{
                .domain = gameplay::EngineQueryDomain::Remap,
                .mode = gameplay::EngineInputMode::Gamepad,
                .causality = gameplay::EngineDecisionCausality::AfterOwnerPublication } }
        });
    {
        auto remapScope = router.EnterScopedOverride(
            gameplay::EngineQueryDomain::Remap,
            gameplay::EngineInputMode::Gamepad,
            44,
            9);
        Require(!router.DecideForSnapshot(0, remapSnapshot, false),
            "Remap scope must fail closed to original");
    }

    const auto scopedSnapshot = gameplay::ProjectEngineModeShadow(
        gameplay::EngineModeProjectionInput{
            .ownerTickToken = 44,
            .inputStateEpoch = 7,
            .contextRevision = 9,
            .runtimeGeneration = 12,
            .recommendations = {
                gameplay::EngineModeRecommendation{
                    .domain = gameplay::EngineQueryDomain::MenuSetPlatform,
                    .mode = gameplay::EngineInputMode::Gamepad,
                    .causality = gameplay::EngineDecisionCausality::AfterOwnerPublication },
                gameplay::EngineModeRecommendation{
                    .domain = gameplay::EngineQueryDomain::GameplayLookTransform,
                    .mode = gameplay::EngineInputMode::KeyboardMouse,
                    .causality = gameplay::EngineDecisionCausality::EventLocalSource }
            }
        });
    {
        auto menuScope = router.EnterScopedOverride(
            gameplay::EngineQueryDomain::MenuSetPlatform,
            gameplay::EngineInputMode::Gamepad,
            44,
            9);
        Require(router.DecideForSnapshot(0, scopedSnapshot, false),
            "verified host menu scope must expose its scoped gamepad value");
        {
            auto lookScope = router.EnterScopedOverride(
                gameplay::EngineQueryDomain::GameplayLookTransform,
                gameplay::EngineInputMode::KeyboardMouse,
                44,
                9);
            Require(!router.DecideForSnapshot(0, scopedSnapshot, true),
                "nested look scope must temporarily override the outer menu scope");
        }
        Require(router.DecideForSnapshot(0, scopedSnapshot, false),
            "destroying an inner TLS scope must restore the outer scope");
    }
    Require(!router.DecideForSnapshot(0, scopedSnapshot, false),
        "destroying the outer TLS scope must restore Original with no leak");

    {
        auto staleScope = router.EnterScopedOverride(
            gameplay::EngineQueryDomain::MenuSetPlatform,
            gameplay::EngineInputMode::Gamepad,
            43,
            9);
        Require(!router.DecideForSnapshot(0, scopedSnapshot, false),
            "stale owner token scope must return original");
    }
}

void RunPresentationProjectionTests()
{
    namespace presentation = dualpad::input_v2::presentation;

    {
        presentation::DeviceFamilyIngressPublisher ingress;
        presentation::SourceEvidenceCollector collector;

        auto publication = ingress.Publish(
            presentation::DeviceFamily::Gamepad,
            presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            100);
        auto frame = collector.CollectAfterDeviceFamilyIngress(publication, GameplayContext(), 101);

        Require(frame.records.size() == 2, "device family change must publish marker followed by source evidence snapshot");
        Require(
            frame.records[0].kind == presentation::SourceEvidenceRecordKind::DeviceFamilyChanged,
            "first record must be DeviceFamilyChanged marker");
        Require(
            frame.records[1].kind == presentation::SourceEvidenceRecordKind::SourceEvidenceSnapshot,
            "second record must be SourceEvidenceSnapshot");
        Require(
            frame.records[0].deviceFamilyChanged.newRevision ==
                frame.records[1].sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision,
            "DeviceFamilyChangedPayload.newRevision must equal paired SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision");
        Require(
            frame.records[1].sourceEvidence.contextRevision == 7,
            "SourceEvidenceSnapshot must pair with the PH2 resolved context revision");

        collector.MarkSyntheticKeyboardScancode(42, 1, 100, 1000);
        Require(collector.IsSyntheticKeyboardWindowActive(1050), "collector must own synthetic keyboard window state");
        Require(collector.ConsumeSyntheticKeyboardScancode(42, 1050), "collector must consume pending synthetic keyboard scancode");
        collector.RecordKeyboardEvidence(true, false, 1060);
        collector.RecordMouseButtonEvidence(true, 1061);
        collector.RecordMouseMoveEvidence(6, 6, 1062);
        collector.RecordGamepadEvidence(true, 1063, 1500);
        Require(collector.HasGamepadLeaseActive(1200), "collector must own gamepad lease state");
        Require(collector.ShouldPromoteMouseMove(10, 120, 1200), "collector must own mouse move accumulator state");
        collector.ResetForContextBoundary(1201);
        Require(!collector.ShouldPromoteMouseMove(10, 120, 1400), "context boundary must reset mouse move evidence");
        Require(collector.GetLatestSnapshot().pointerSignal == presentation::PointerSignal::None, "context boundary reset must clear pointer signal");
    }

    {
        presentation::DeviceFamilyIngressPublisher ingress;
        presentation::SourceEvidenceCollector collector;
        presentation::PresentationProjection projection;

        const auto frame = collector.CollectAfterDeviceFamilyIngress(
            ingress.Publish(
                presentation::DeviceFamily::Gamepad,
                presentation::DeviceFamilyEvidenceSource::RawInputIngress,
                200),
            MenuContext(),
            201);
        const auto& snapshot = frame.records.back().sourceEvidence;

        presentation::PublishedGameplayPresentation gameplay{};
        gameplay.engineOwner = presentation::PresentationOwner::KeyboardMouse;
        gameplay.menuEntryOwner = presentation::PresentationOwner::Gamepad;
        gameplay.gameplayPresentationRevision = 3;

        const auto published = projection.Project(snapshot, MenuContext(), gameplay);
        Require(published.family == presentation::DeviceFamily::Gamepad, "projection must copy family from source evidence");
        Require(
            published.deviceFamilyRevision == snapshot.deviceFamilyEvidence.deviceFamilyRevision,
            "projection must copy deviceFamilyRevision from source evidence");
        Require(
            published.owner == presentation::PresentationOwner::Gamepad,
            "gameplay to menu first projection must inherit PublishedGameplayPresentation.menuEntryOwner");
        Require(
            published.presentationPolicyId == "PolicyOnlyPH2MayChoose",
            "PresentationProjection must forward PH2 presentationPolicyId without deriving it from menu semantics");
        Require(
            published.uiContextId == MenuContext().uiContextId,
            "PresentationProjection must forward PH2 uiContextId");
        Require(
            published.actionSetStack == MenuContext().actionSetStack,
            "PresentationProjection must forward PH2 action set stack");
        Require(published.epoch == 1, "dirty first publish must advance epoch");
        Require(
            published.targetMenuName == "Journal Menu" &&
                published.targetMenuInstanceId == 1 &&
                published.targetMenuPtr == 0x1000 &&
                published.targetMenuMoviePtr == 0x2000 &&
                published.menuStackRevision == 4,
            "presentation publication must carry the stable target captured by ContextResolver");

        auto keyboardTakeover = snapshot;
        keyboardTakeover.keyboardEvidence = true;
        keyboardTakeover.deviceFamilyEvidence.family = presentation::DeviceFamily::KeyboardMouse;
        keyboardTakeover.deviceFamilyEvidence.deviceFamilyRevision = 2;
        const auto keyboardPublished = projection.Project(keyboardTakeover, MenuContext(), gameplay);
        Require(
            keyboardPublished.owner == presentation::PresentationOwner::KeyboardMouse,
            "menu keyboard evidence must take over owner from inherited gamepad");
        Require(
            presentation::HasDirtyFlag(keyboardPublished.dirty, presentation::PresentationDirtyFlags::Owner),
            "menu keyboard takeover must set dirty.Owner");
        Require(keyboardPublished.epoch == published.epoch + 1, "menu keyboard takeover must advance epoch");

        const auto unchanged = projection.Project(keyboardTakeover, MenuContext(), gameplay);
        Require(unchanged.owner == presentation::PresentationOwner::KeyboardMouse, "unchanged menu publish must keep owner");
        Require(unchanged.dirty == presentation::PresentationDirtyFlags::None, "unchanged menu publish must not set dirty flags");
        Require(unchanged.epoch == keyboardPublished.epoch, "unchanged menu publish must not jitter epoch");

        auto gamepadReclaim = keyboardTakeover;
        gamepadReclaim.keyboardEvidence = false;
        gamepadReclaim.gamepadEvidence = true;
        gamepadReclaim.gamepadLease = true;
        gamepadReclaim.deviceFamilyEvidence.family = presentation::DeviceFamily::Gamepad;
        gamepadReclaim.deviceFamilyEvidence.deviceFamilyRevision = 3;
        const auto gamepadPublished = projection.Project(gamepadReclaim, MenuContext(), gameplay);
        Require(
            gamepadPublished.owner == presentation::PresentationOwner::Gamepad,
            "menu gamepad evidence or lease must reclaim owner");
        Require(
            presentation::HasDirtyFlag(gamepadPublished.dirty, presentation::PresentationDirtyFlags::Owner),
            "menu gamepad reclaim must set dirty.Owner");
        Require(gamepadPublished.epoch == unchanged.epoch + 1, "menu gamepad reclaim must advance epoch");

        gameplay.engineOwner = presentation::PresentationOwner::KeyboardMouse;
        const auto gameplayPublished = projection.Project(gamepadReclaim, GameplayContext(), gameplay);
        Require(
            gameplayPublished.owner == presentation::PresentationOwner::KeyboardMouse,
            "menu to gameplay must consume PublishedGameplayPresentation.engineOwner");
    }

    {
        presentation::PresentationProjection projection;

        auto snapshot = presentation::SourceEvidenceSnapshot{};
        snapshot.deviceFamilyEvidence = presentation::PublishedDeviceFamilyEvidence{
            .family = presentation::DeviceFamily::Gamepad,
            .deviceFamilyRevision = 11,
            .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            .publishedTick = 250
        };
        snapshot.gamepadEvidence = true;
        snapshot.gamepadLease = true;
        snapshot.contextRevision = MenuContext().contextRevision;

        presentation::PublishedGameplayPresentation gameplay{};
        gameplay.engineOwner = presentation::PresentationOwner::KeyboardMouse;
        gameplay.menuEntryOwner = presentation::PresentationOwner::KeyboardMouse;
        gameplay.gameplayPresentationRevision = 0;

        const auto published = projection.Project(snapshot, MenuContext(), gameplay);
        Require(
            published.owner == presentation::PresentationOwner::Gamepad,
            "startup menu gamepad evidence must override the default gameplay menu entry owner");
        Require(
            published.reason == presentation::PresentationDecisionReason::MenuSourceEvidence,
            "startup menu gamepad takeover must be attributed to current source evidence");
    }

    {
        presentation::PresentationProjection projection;

        auto snapshot = presentation::SourceEvidenceSnapshot{};
        snapshot.deviceFamilyEvidence = presentation::PublishedDeviceFamilyEvidence{
            .family = presentation::DeviceFamily::Gamepad,
            .deviceFamilyRevision = 12,
            .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            .publishedTick = 260
        };
        snapshot.gamepadEvidence = true;
        snapshot.contextRevision = MenuContext().contextRevision;

        auto degradedContext = MenuContext();
        degradedContext.uiContextId = dualpad::input_v2::context::UiContextId::UnknownTrackedMenu;
        degradedContext.menuObserverCompleteness = dualpad::input_v2::menu::ObserverCompleteness::Partial;
        degradedContext.identityQuality = dualpad::input_v2::menu::MenuIdentityQuality::DegradedIdentity;
        degradedContext.menuIdentityDegraded = true;

        presentation::PublishedGameplayPresentation gameplay{};
        const auto published = projection.Project(snapshot, degradedContext, gameplay);
        Require(
            published.menuRefreshEligibility == presentation::MenuRefreshEligibility::ObserverPartial,
            "PresentationProjection must publish observer partial refresh eligibility");

        auto stableContext = MenuContext();
        stableContext.uiContextId = dualpad::input_v2::context::UiContextId::UnknownTrackedMenu;
        const auto stableGeneric = projection.Project(snapshot, stableContext, gameplay);
        Require(
            stableGeneric.menuRefreshEligibility == presentation::MenuRefreshEligibility::EligibleStableMenu,
            "stable generic menu identity must remain eligible for platform refresh");

        auto unavailableContext = MenuContext();
        unavailableContext.menuObserverCompleteness =
            dualpad::input_v2::menu::ObserverCompleteness::Unavailable;
        const auto unavailable = projection.Project(snapshot, unavailableContext, gameplay);
        Require(
            unavailable.menuRefreshEligibility == presentation::MenuRefreshEligibility::ObserverUnavailable,
            "observer unavailable must publish a distinct menu refresh ineligibility reason");

        auto identityDegradedContext = MenuContext();
        identityDegradedContext.identityQuality =
            dualpad::input_v2::menu::MenuIdentityQuality::DegradedIdentity;
        const auto identityDegraded = projection.Project(snapshot, identityDegradedContext, gameplay);
        Require(
            identityDegraded.menuRefreshEligibility == presentation::MenuRefreshEligibility::IdentityDegraded,
            "degraded menu identity must publish a distinct menu refresh ineligibility reason");

        auto noStableTargetContext = MenuContext();
        noStableTargetContext.topMenuInstanceId.reset();
        const auto noStableTarget = projection.Project(snapshot, noStableTargetContext, gameplay);
        Require(
            noStableTarget.menuRefreshEligibility == presentation::MenuRefreshEligibility::NoStableTarget,
            "complete menu snapshots without a stable target must not be refresh eligible");

        const auto notMenu = projection.Project(snapshot, GameplayContext(), gameplay);
        Require(
            notMenu.menuRefreshEligibility == presentation::MenuRefreshEligibility::NotMenu,
            "gameplay contexts must publish NotMenu refresh eligibility");
    }

    {
        presentation::PresentationProjection projection;
        presentation::SkyrimCompatibilitySurface compat;
        presentation::SourceEvidenceSnapshot snapshot{};
        snapshot.deviceFamilyEvidence = presentation::PublishedDeviceFamilyEvidence{
            .family = presentation::DeviceFamily::KeyboardMouse,
            .deviceFamilyRevision = 12,
            .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            .publishedTick = 300
        };
        snapshot.pointerSignal = presentation::PointerSignal::PointerActive;
        snapshot.contextRevision = 9;

        presentation::PublishedGameplayPresentation gameplay{};
        gameplay.engineOwner = presentation::PresentationOwner::KeyboardMouse;
        gameplay.menuEntryOwner = presentation::PresentationOwner::KeyboardMouse;

        auto context = GameplayContext();
        context.contextRevision = 9;
        const auto published = projection.Project(snapshot, context, gameplay);
        compat.Commit(published);
        compat.ForceOriginalHookOutputsForTests(presentation::LegacyCompatibilitySurface{
            .isUsingGamepad = false,
            .gamepadControlsCursor = false,
            .gamepadDeviceEnabled = false
        });
        Require(!compat.IsUsingGamepadHook(), "compat hook must read committed published owner");
        Require(!compat.GamepadControlsCursorHook(), "pointer active KBM state must publish KeyboardMouse cursor owner");

        compat.EnableRollback(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = true,
                .gamepadControlsCursor = false,
                .gamepadDeviceEnabled = true
            });
        Require(!compat.IsUsingGamepadHook(), "rollback helper must not override committed input_v2 owner");
        Require(!compat.GamepadControlsCursorHook(), "rollback helper must not override committed cursor output");
        Require(
            compat.GetCommittedState().contextRevision == 9,
            "rollback must not mutate PH2-derived context truth in committed presentation state");

        compat.DisableRollback();
        const auto parity = compat.CompareShadowParity(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = false,
                .gamepadControlsCursor = false,
                .gamepadDeviceEnabled = false
            },
            true);
        Require(parity.passes, "shadow parity must pass when legacy and projected compatibility outputs match");
        Require(parity.contextRevision == 9, "shadow parity must carry contextRevision for diff logs");
        Require(parity.epoch == published.epoch, "shadow parity must carry epoch for refresh parity");
        const auto diff = compat.CompareShadowParity(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = true,
                .gamepadControlsCursor = false,
                .gamepadDeviceEnabled = false
            },
            true);
        Require(!diff.passes, "shadow parity must fail on projected hook output mismatch");
        Require(diff.diffs.size() == 1 && diff.diffs.front() == "isUsingGamepad", "shadow parity must report hook field diffs");
    }

    {
        presentation::SkyrimCompatibilitySurface compat;
        compat.ResetRefreshStateForTests();
        std::size_t queuedRefreshes = 0;
        compat.SetMenuRefreshTaskSinkForTests([&](auto) {
            ++queuedRefreshes;
            compat.CompleteQueuedRefreshForTests();
            return true;
        });

        presentation::PublishedPresentationState menuGamepad{};
        menuGamepad.owner = presentation::PresentationOwner::Gamepad;
        menuGamepad.navigationOwner = presentation::NavigationOwner::Gamepad;
        menuGamepad.cursorOwner = presentation::CursorOwner::Gamepad;
        menuGamepad.uiContextId = dualpad::input_v2::context::UiContextId::Journal;
        menuGamepad.menuRefreshEligibility = presentation::MenuRefreshEligibility::EligibleStableMenu;
        menuGamepad.targetMenuName = "Journal Menu";
        menuGamepad.targetMenuInstanceId = 1;
        menuGamepad.targetMenuPtr = 0x1000;
        menuGamepad.targetMenuMoviePtr = 0x2000;
        menuGamepad.menuStackRevision = 10;
        menuGamepad.contextRevision = 10;
        menuGamepad.epoch = 1;
        menuGamepad.dirty = presentation::PresentationDirtyFlags::Owner;
        compat.Commit(menuGamepad);

        Require(
            compat.RefreshMenusIfNeeded(),
            "OpeningNewMenuWithSameOwner_RefreshesPlatformOnce owner dirty must queue platform refresh");
        Require(queuedRefreshes == 1, "OpeningNewMenuWithSameOwner_RefreshesPlatformOnce queued count");
        Require(
            !compat.RefreshMenusIfNeeded(),
            "identical presentation epoch must not queue duplicate menu platform refreshes");
        Require(queuedRefreshes == 1, "identical epoch must not spam platform refresh");

        presentation::PublishedPresentationState gameplayOwnerDirty = menuGamepad;
        gameplayOwnerDirty.uiContextId = dualpad::input_v2::context::UiContextId::None;
        gameplayOwnerDirty.menuRefreshEligibility = presentation::MenuRefreshEligibility::NotMenu;
        gameplayOwnerDirty.contextRevision = 11;
        gameplayOwnerDirty.epoch = 2;
        gameplayOwnerDirty.dirty = presentation::PresentationDirtyFlags::Owner;
        compat.Commit(gameplayOwnerDirty);
        Require(
            !compat.RefreshMenusIfNeeded(),
            "GameplayOwnerDirty_DoesNotQueueMenuPlatformRefresh");
        Require(queuedRefreshes == 1, "gameplay owner dirty must not queue a stale menu refresh task");

        presentation::PublishedPresentationState stableGenericMenu = menuGamepad;
        stableGenericMenu.uiContextId = dualpad::input_v2::context::UiContextId::UnknownTrackedMenu;
        stableGenericMenu.menuRefreshEligibility = presentation::MenuRefreshEligibility::EligibleStableMenu;
        stableGenericMenu.contextRevision = 12;
        stableGenericMenu.actionSetStack.baseSetId = "MenuBase";
        stableGenericMenu.actionSetStack.layerIds = { "UnknownTrackedMenuLayer" };
        stableGenericMenu.actionSetStack.scopeAnchorIds = { "MenuBase", "UnknownTrackedMenuLayer" };
        stableGenericMenu.presentationPolicyId = "Menu";
        stableGenericMenu.epoch = 3;
        stableGenericMenu.dirty = presentation::PresentationDirtyFlags::Context;
        compat.Commit(stableGenericMenu);
        Require(
            compat.RefreshMenusIfNeeded(),
            "StableGenericMenu_QueuesPlatformRefresh");
        Require(queuedRefreshes == 2, "stable generic Main Menu context must refresh platform state");

        presentation::PublishedPresentationState degradedUnknownMenu = stableGenericMenu;
        degradedUnknownMenu.menuRefreshEligibility = presentation::MenuRefreshEligibility::ObserverPartial;
        degradedUnknownMenu.contextRevision = 13;
        degradedUnknownMenu.epoch = 4;
        degradedUnknownMenu.dirty = presentation::PresentationDirtyFlags::Context;
        compat.Commit(degradedUnknownMenu);
        Require(
            !compat.RefreshMenusIfNeeded(),
            "ObserverDegradedUnknownMenu_DoesNotQueuePlatformRefresh");
        Require(queuedRefreshes == 2, "degraded unknown menu must wait for a stable tracked menu before refresh");

        presentation::PublishedPresentationState contextOnly = menuGamepad;
        contextOnly.uiContextId = dualpad::input_v2::context::UiContextId::Favorites;
        contextOnly.contextRevision = 14;
        contextOnly.actionSetStack.baseSetId = "MenuBase";
        contextOnly.actionSetStack.layerIds = { "FavoritesLayer" };
        contextOnly.actionSetStack.scopeAnchorIds = { "MenuBase", "FavoritesLayer" };
        contextOnly.epoch = 5;
        contextOnly.dirty = presentation::PresentationDirtyFlags::Context;
        compat.Commit(contextOnly);
        Require(
            compat.RefreshMenusIfNeeded(),
            "SwitchingMenuContextWithSameOwner_QueuesPlatformRefresh");
        Require(queuedRefreshes == 3, "context-only refresh must queue exactly once");

        presentation::PublishedPresentationState policyChange = contextOnly;
        policyChange.presentationPolicyId = "MenuPolicyChanged";
        policyChange.epoch = 6;
        policyChange.dirty = presentation::PresentationDirtyFlags::Policy;
        compat.Commit(policyChange);
        Require(
            compat.RefreshMenusIfNeeded(),
            "policy dirty presentation publish must refresh menu platform state");
        Require(queuedRefreshes == 4, "policy refresh must queue exactly once");

        presentation::PublishedPresentationState actionSetChange = policyChange;
        actionSetChange.actionSetStack.layerIds = { "FavoritesLayer" };
        actionSetChange.actionSetStack.scopeAnchorIds = { "MenuBase", "FavoritesLayer" };
        actionSetChange.epoch = 7;
        actionSetChange.dirty = presentation::PresentationDirtyFlags::ActionSets;
        compat.Commit(actionSetChange);
        Require(
            compat.RefreshMenusIfNeeded(),
            "action-set/prompt-affecting presentation publish must queue platform refresh");
        Require(queuedRefreshes == 5, "action-set refresh must queue exactly once");

        compat.ResetRefreshStateForTests();
        compat.SetMenuRefreshTaskSinkForTests([](auto) {
            return false;
        });
        presentation::PublishedPresentationState unavailable = menuGamepad;
        unavailable.epoch = 10;
        unavailable.dirty = presentation::PresentationDirtyFlags::Owner;
        compat.Commit(unavailable);
        Require(
            !compat.RefreshMenusIfNeeded(),
            "RefreshQueueUnavailable_DoesNotConsumeEpoch first queue attempt must fail");
        compat.SetMenuRefreshTaskSinkForTests([&](auto) {
            ++queuedRefreshes;
            compat.CompleteQueuedRefreshForTests();
            return true;
        });
        Require(
            compat.RefreshMenusIfNeeded(),
            "RefreshQueueUnavailable_DoesNotConsumeEpoch retry must still see the same pending epoch");
    }

    {
        const presentation::MenuRefreshTarget captured{
            .menuName = "Main Menu",
            .instanceId = 7,
            .menuPtr = 0x1100,
            .moviePtr = 0x2200,
            .menuStackRevision = 3,
            .contextRevision = 4,
            .presentationEpoch = 5
        };
        auto current = captured;
        presentation::LiveMenuRefreshTarget live{
            .uiAvailable = true,
            .menuPtr = 0x1100,
            .moviePtr = 0x2200,
            .rootReady = true,
            .ownedCallbackReady = true
        };
        Require(
            presentation::ValidateMenuRefreshTarget(captured, current, live) ==
                presentation::MenuRefreshTargetValidation::ReadyOwnedCallback,
            "allowlisted stable target with callback must prefer the DualPad-owned callback");
        live.ownedCallbackReady = false;
        Require(
            presentation::ValidateMenuRefreshTarget(captured, current, live) ==
                presentation::MenuRefreshTargetValidation::ReadyRefreshPlatform,
            "allowlisted stable target may use target-only RefreshPlatform when callback is absent");
        live.moviePtr = 0;
        Require(
            presentation::ValidateMenuRefreshTarget(captured, current, live) ==
                presentation::MenuRefreshTargetValidation::DeferredNotReady,
            "target with null movie must defer without touching another menu");
        live.moviePtr = 0x2200;
        live.rootReady = false;
        Require(
            presentation::ValidateMenuRefreshTarget(captured, current, live) ==
                presentation::MenuRefreshTargetValidation::DeferredNotReady,
            "target with unavailable movie root must defer without calling RefreshPlatform");
        live.rootReady = true;
        live.menuPtr = 0x3300;
        Require(
            presentation::ValidateMenuRefreshTarget(captured, current, live) ==
                presentation::MenuRefreshTargetValidation::Superseded,
            "replaced instance must cancel stale refresh");
        live.menuPtr = 0x1100;
        current.menuStackRevision += 1;
        Require(
            presentation::ValidateMenuRefreshTarget(captured, current, live) ==
                presentation::MenuRefreshTargetValidation::Superseded,
            "changed menu stack revision must cancel stale refresh");

        presentation::SkyrimCompatibilitySurface compat;
        auto firstTarget = EligibleMenuPresentation(10, 20);
        auto replacedTarget = firstTarget;
        replacedTarget.targetMenuInstanceId += 1;
        Require(
            compat.MakeRefreshKeyForTests(firstTarget) != compat.MakeRefreshKeyForTests(replacedTarget),
            "replacement instance must produce a distinct refresh key");

        for (const auto deniedName : { "Loading Menu", "Fader Menu", "MessageBoxMenu" }) {
            auto denied = captured;
            denied.menuName = deniedName;
            Require(
                presentation::ValidateMenuRefreshTarget(denied, denied, live) ==
                    presentation::MenuRefreshTargetValidation::Disallowed,
                "loading/fader/message box targets must be denied by refresh allowlist");
        }
    }

    {
        presentation::SkyrimCompatibilitySurface compat;
        compat.ResetRefreshStateForTests();
        std::size_t queuedRefreshes = 0;
        compat.SetMenuRefreshTaskSinkForTests([&](auto) {
            ++queuedRefreshes;
            return true;
        });

        auto first = EligibleMenuPresentation(1, 20);
        first.dirty = presentation::PresentationDirtyFlags::Owner;
        compat.Commit(first);
        Require(
            compat.RefreshMenusIfNeeded(),
            "MenuRefresh_InFlightRequest must queue the first refresh request");
        Require(queuedRefreshes == 1, "first refresh request must be in flight");

        auto second = EligibleMenuPresentation(2, 21);
        second.actionSetStack.layerIds = { "MenuLayer", "PromptLayer" };
        second.actionSetStack.scopeAnchorIds = { "MenuBase", "MenuLayer", "PromptLayer" };
        second.dirty = presentation::PresentationDirtyFlags::ActionSets;
        compat.Commit(second);
        Require(
            !compat.RefreshMenusIfNeeded(),
            "MenuRefresh_InFlightRequest must not start a second task while the first is in flight");
        Require(queuedRefreshes == 1, "pending latest request must be latched, not started immediately");

        compat.CompleteQueuedRefreshForTests();
        Require(
            queuedRefreshes == 2,
            "MenuRefresh_PendingLatest must queue after the executed request completes");
        Require(
            !compat.RefreshMenusIfNeeded(),
            "MenuRefresh_PendingLatest must be in flight after it is scheduled");

        compat.CompleteQueuedRefreshForTests();
        Require(
            !compat.RefreshMenusIfNeeded(),
            "MenuRefresh_CompletionOwnership must not requeue once the captured latest request completes");
        Require(queuedRefreshes == 2, "completed latest request must not be duplicated");
    }

    {
        presentation::SkyrimCompatibilitySurface compat;
        compat.ResetRefreshStateForTests();

        auto first = EligibleMenuPresentation(61, 70);
        first.gameplayPresentationRevision = 1;
        first.dirty = presentation::PresentationDirtyFlags::Owner;

        auto revisionOnly = first;
        revisionOnly.gameplayPresentationRevision = 2;
        revisionOnly.dirty = presentation::PresentationDirtyFlags::None;
        Require(
            compat.MakeRefreshKeyForTests(first) == compat.MakeRefreshKeyForTests(revisionOnly),
            "MenuRefresh_GameplayRevisionOnlyChange must not change refresh key when no refresh-relevant dirty flag changed");
    }

    {
        presentation::SkyrimCompatibilitySurface compat;
        compat.ResetRefreshStateForTests();
        std::size_t queuedRefreshes = 0;
        compat.SetMenuRefreshTaskSinkForTests([&](auto) {
            ++queuedRefreshes;
            return true;
        });

        auto state = EligibleMenuPresentation(30, 40);
        state.dirty = presentation::PresentationDirtyFlags::Owner;
        compat.Commit(state);
        Require(
            compat.RefreshMenusIfNeeded(),
            "MenuRefresh_NoUiSingletonReady must queue the initial refresh request");
        compat.DeferQueuedRefreshForTests();
        compat.DeferQueuedRefreshForTests();
        compat.DeferQueuedRefreshForTests();
        compat.DeferQueuedRefreshForTests();
        Require(
            queuedRefreshes == 4,
            "MenuRefresh_DeferredNotReady must retry readiness with a bounded attempt count");
        Require(
            !compat.RefreshMenusIfNeeded(),
            "MenuRefresh_DeferredNotReady must not spin after exhausting bounded deferred retries");
        auto stableTickWithoutDirty = state;
        stableTickWithoutDirty.dirty = presentation::PresentationDirtyFlags::None;
        compat.Commit(stableTickWithoutDirty);
        Require(
            compat.RefreshMenusIfNeeded(),
            "MenuRefresh_DeferredNotReady must preserve held intent across a later stable tick without dirty");
        Require(queuedRefreshes == 5, "held deferred intent must requeue once on the later stable tick");
        compat.DeferQueuedRefreshForTests();
        auto identicalStableTick = stableTickWithoutDirty;
        compat.Commit(identicalStableTick);
        Require(
            !compat.RefreshMenusIfNeeded(),
            "MenuRefresh_DeferredNotReady must not requeue held intent on every identical stable tick");
        Require(queuedRefreshes == 5, "held deferred intent must not spin on unchanged stable ticks");
        compat.CompleteQueuedRefreshForTests();
        compat.SetMenuRefreshTaskSinkForTests({});
        compat.ResetRefreshStateForTests();
    }

    {
        presentation::SkyrimCompatibilitySurface compat;
        compat.ResetRefreshStateForTests();
        std::size_t queueAttempts = 0;
        std::size_t successfulQueues = 0;

        auto first = EligibleMenuPresentation(41, 50);
        first.dirty = presentation::PresentationDirtyFlags::Owner;
        auto second = EligibleMenuPresentation(42, 51);
        second.actionSetStack.layerIds = { "MenuLayer", "PromptLayer" };
        second.actionSetStack.scopeAnchorIds = { "MenuBase", "MenuLayer", "PromptLayer" };
        second.dirty = presentation::PresentationDirtyFlags::ActionSets;

        compat.SetMenuRefreshTaskSinkForTests([&](auto) {
            ++queueAttempts;
            compat.Commit(second);
            return false;
        });
        compat.Commit(first);
        Require(
            !compat.RefreshMenusIfNeeded(),
            "MenuRefresh_QueueFailureDoesNotOverwriteNewerPendingLatest first queue attempt must fail");

        compat.SetMenuRefreshTaskSinkForTests([&](auto) {
            ++queueAttempts;
            ++successfulQueues;
            return true;
        });
        Require(
            compat.RefreshMenusIfNeeded(),
            "MenuRefresh_QueueFailureDoesNotOverwriteNewerPendingLatest must retain the newer pending request");
        Require(queueAttempts == 2, "queue failure retry must only schedule the retained newer request");
        Require(successfulQueues == 1, "newer pending request must be successfully queued once");
        compat.CompleteQueuedRefreshForTests();
        Require(
            !compat.RefreshMenusIfNeeded(),
            "MenuRefresh_QueueFailureDoesNotOverwriteNewerPendingLatest must not resurrect the failed stale request");
    }

    {
        const auto site = presentation::detail::MakeVfuncPatchSite(0x1000, 0x8);
        Require(
            site.relocationBase == 0x1000,
            "vfunc hook site must pass the vtable base to write_vfunc instead of a pre-offset slot address");
        Require(site.index == 0x8, "vfunc hook site must carry the single vfunc index offset");

        auto state = presentation::detail::InstallState::NotInstalled;
        Require(presentation::detail::CanBeginInstall(state), "fresh install state must allow install start");
        state = presentation::detail::BeginInstall(state);
        Require(state == presentation::detail::InstallState::Installing, "begin install must enter Installing state");
        state = presentation::detail::FailInstall(state);
        Require(state == presentation::detail::InstallState::Failed, "failed install attempt must enter Failed state");
        Require(!presentation::detail::CanBeginInstall(state), "failed install state must not silently retry");

        const auto success = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::Success,
            "installed");
        Require(success.installed, "success hook install result must be installed");
        Require(!presentation::IsHookInstallFailure(success), "success hook install result must not fail closed");
        Require(
            presentation::ToString(success.status) == std::string_view("success"),
            "success hook install result must expose a stable debug status");

        const auto unsupported = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::UnsupportedRuntime,
            "unsupported_runtime_1.6.640");
        Require(!unsupported.installed, "unsupported runtime hook result must not be installed");
        Require(!presentation::IsHookInstallFailure(unsupported), "CompatSurfaceUnsupportedRuntime_DegradesPresentationOnly");
        Require(
            unsupported.operationalState == dualpad::input::patching::HookOperationalState::SafePassthrough &&
                unsupported.disposition == dualpad::input::patching::HookFailureDisposition::NotRequired,
            "unsupported runtime must expose safe passthrough without rollback");
        Require(
            presentation::ToDebugString(unsupported).find("unsupported_runtime") != std::string::npos,
            "unsupported runtime hook result must expose debug reason");

        const auto mismatch = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::SignatureMismatch,
            "is_using_gamepad_call_signature_mismatch");
        Require(!mismatch.installed, "signature mismatch hook result must not be installed");
        Require(!presentation::IsHookInstallFailure(mismatch), "SkyrimCompatSurface hook mismatch must not fail closed by default");

        const auto alreadyInstalled = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::AlreadyInstalled,
            "install_already_completed");
        Require(alreadyInstalled.installed, "already installed hook result must remain installed");
        Require(!presentation::IsHookInstallFailure(alreadyInstalled), "already installed must not fail closed");

        const auto failed = presentation::detail::EvaluateHookTransactionResult(
            dualpad::input::patching::PatchTransactionOutcome::FailedNoWrite,
            "exception_before_patch_started");
        Require(failed.status == presentation::HookInstallStatus::Failed, "pre-patch exception must be failed");
        Require(!presentation::IsHookInstallFailure(failed), "SkyrimCompatSurface failed hook install must not fail closed by default");

        const auto partial = presentation::detail::EvaluateHookTransactionResult(
            dualpad::input::patching::PatchTransactionOutcome::RolledBack,
            "exception_after_patch_started");
        Require(
            partial.status == presentation::HookInstallStatus::PartialInstall,
            "post-patch exception must be partial install");
        Require(!presentation::IsHookInstallFailure(partial), "successfully rolled-back partial install must be safe passthrough");
        Require(
            partial.operationalState == dualpad::input::patching::HookOperationalState::SafePassthrough &&
                partial.disposition == dualpad::input::patching::HookFailureDisposition::RolledBack,
            "rolled-back compat patch must expose exact recovery disposition");

        const auto unsafePartial = presentation::detail::EvaluateHookTransactionResult(
            dualpad::input::patching::PatchTransactionOutcome::UnsafePartial,
            "rollback_expected_current_mismatch");
        Require(
            unsafePartial.status == presentation::HookInstallStatus::UnsafePartial &&
                presentation::IsHookInstallFailure(unsafePartial) &&
                unsafePartial.operationalState == dualpad::input::patching::HookOperationalState::UnsafePartial &&
                unsafePartial.disposition == dualpad::input::patching::HookFailureDisposition::FailClosed,
            "unsafe partial compat hook must be explicitly fail-closed");

        presentation::SkyrimCompatibilitySurface compat;
        compat.Commit(presentation::PublishedPresentationState{});
        compat.ForceOriginalHookOutputsForTests(presentation::LegacyCompatibilitySurface{
            .isUsingGamepad = true,
            .gamepadControlsCursor = true,
            .gamepadDeviceEnabled = true
        });
        compat.ForceHooksEnabledForTests(false);
        Require(compat.IsUsingGamepadHook(), "CompatSurfacePartialInstall_DoesNotForceKeyboardMode isUsingGamepad");
        Require(compat.GamepadControlsCursorHook(), "CompatSurfacePartialInstall_DoesNotForceKeyboardMode cursor");
        Require(compat.IsGamepadDeviceEnabledHook(true), "CompatSurfacePartialInstall_DoesNotForceKeyboardMode device enabled");
        compat.ForceHooksEnabledForTests(true);
        Require(compat.IsUsingGamepadHook(), "enabled engine gateway must still return the original value without a verified scope");

        Require(
            presentation::detail::EvaluateHookInstallGate(true, true).status ==
                presentation::HookInstallStatus::Success,
            "matching runtime and signatures may install");
        Require(
            presentation::detail::EvaluateHookInstallGate(false, true).status ==
                presentation::HookInstallStatus::UnsupportedRuntime,
            "unsupported runtime must stop before patching");
        Require(
            presentation::detail::EvaluateHookInstallGate(true, false).status ==
                presentation::HookInstallStatus::SignatureMismatch,
            "signature mismatch must stop before patching");
    }

    {
        presentation::GameplayPresentationAdapter adapter;
        const auto first = adapter.PublishForTests(
            presentation::GameplayPresentationAdapterInput{
                .engineOwner = presentation::PresentationOwner::Gamepad,
                .menuEntryOwner = presentation::PresentationOwner::Gamepad,
                .reason = presentation::GameplayPresentationReasonCode::CoordinatorPublished,
                .publishedTick = 1
            });
        Require(first.gameplayPresentationRevision == 1, "adapter must increment gameplayPresentationRevision on published owner changes");

        const auto unchanged = adapter.PublishForTests(
            presentation::GameplayPresentationAdapterInput{
                .engineOwner = presentation::PresentationOwner::Gamepad,
                .menuEntryOwner = presentation::PresentationOwner::Gamepad,
                .reason = presentation::GameplayPresentationReasonCode::CoordinatorPublished,
                .publishedTick = 2
            });
        Require(unchanged.gameplayPresentationRevision == 1, "adapter must not increment gameplayPresentationRevision on unchanged published state");

        const auto resynced = adapter.PublishCleanBaselineForTests(3);
        Require(resynced.gameplayPresentationRevision == 2, "adapter explicit clean baseline must increment gameplayPresentationRevision");
    }
}

int main()
{
    try {
        RunEngineHookIdentityTests();
        RunEngineOriginalFirstTests();
        RunEngineShadowRouterTests();
        RunOrderedActivityRoutingAndIndependentProjectionTests();
        RunCursorPlanAckShadowTests();
        RunPresentationProjectionTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
