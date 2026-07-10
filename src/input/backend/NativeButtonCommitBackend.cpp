#include "pch.h"
#include "input/backend/NativeButtonCommitBackend.h"

#include "input/Action.h"
#include "input/AuthoritativePollState.h"
#include "input/GameplayKbmFactTracker.h"
#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"
#include "input/XInputButtonSerialization.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/NativeActionDescriptor.h"
#include "input/injection/UpstreamGamepadHook.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/gameplay/DualPadRuntime.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"

#include <chrono>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    namespace
    {
        namespace context = dualpad::input_v2::context;
        namespace menu = dualpad::input_v2::menu;
        namespace presentation = dualpad::input_v2::presentation;

        const char* ToTraceString(NativeButtonCommitTranslationKind kind)
        {
            switch (kind) {
            case NativeButtonCommitTranslationKind::Request:
                return "Request";
            case NativeButtonCommitTranslationKind::Noop:
                return "Noop";
            case NativeButtonCommitTranslationKind::Invalid:
            default:
                return "Invalid";
            }
        }

        const char* ToTraceString(context::HostMode mode)
        {
            switch (mode) {
            case context::HostMode::Menu:
                return "Menu";
            case context::HostMode::Gameplay:
            default:
                return "Gameplay";
            }
        }

        const char* ToTraceString(menu::ObserverCompleteness completeness)
        {
            switch (completeness) {
            case menu::ObserverCompleteness::Complete:
                return "Complete";
            case menu::ObserverCompleteness::Partial:
                return "Partial";
            case menu::ObserverCompleteness::Unavailable:
            default:
                return "Unavailable";
            }
        }

        const char* ToTraceString(menu::MenuIdentityQuality quality)
        {
            switch (quality) {
            case menu::MenuIdentityQuality::StablePointer:
                return "StablePointer";
            case menu::MenuIdentityQuality::FingerprintRebound:
                return "FingerprintRebound";
            case menu::MenuIdentityQuality::DegradedIdentity:
            default:
                return "DegradedIdentity";
            }
        }

        const char* ToTraceString(presentation::PresentationOwner owner)
        {
            switch (owner) {
            case presentation::PresentationOwner::Gamepad:
                return "Gamepad";
            case presentation::PresentationOwner::KeyboardMouse:
            default:
                return "KeyboardMouse";
            }
        }

        const char* ToTraceString(presentation::NavigationOwner owner)
        {
            switch (owner) {
            case presentation::NavigationOwner::Gamepad:
                return "Gamepad";
            case presentation::NavigationOwner::KeyboardMouse:
                return "KeyboardMouse";
            case presentation::NavigationOwner::None:
            default:
                return "None";
            }
        }

        const char* ToTraceString(presentation::CursorOwner owner)
        {
            switch (owner) {
            case presentation::CursorOwner::Gamepad:
                return "Gamepad";
            case presentation::CursorOwner::KeyboardMouse:
            default:
                return "KeyboardMouse";
            }
        }

        const char* ToTraceString(presentation::MenuRefreshEligibility eligibility)
        {
            switch (eligibility) {
            case presentation::MenuRefreshEligibility::EligibleStableMenu:
                return "EligibleStableMenu";
            case presentation::MenuRefreshEligibility::ObserverPartial:
                return "ObserverPartial";
            case presentation::MenuRefreshEligibility::ObserverUnavailable:
                return "ObserverUnavailable";
            case presentation::MenuRefreshEligibility::IdentityDegraded:
                return "IdentityDegraded";
            case presentation::MenuRefreshEligibility::NoStableTarget:
                return "NoStableTarget";
            case presentation::MenuRefreshEligibility::NotMenu:
            default:
                return "NotMenu";
            }
        }

        std::uint8_t ToTraceDirtyBits(presentation::PresentationDirtyFlags flags)
        {
            return static_cast<std::uint8_t>(flags);
        }

        bool TraceSlotIsDown(const PollCommitSlot& slot)
        {
            return slot.token.active &&
                slot.token.downSubmitted &&
                !slot.token.releaseSubmitted;
        }

        bool TraceSlotIsManaged(const PollCommitSlot& slot)
        {
            if (slot.actionId == RE::BSFixedString(actions::Sprint.data()) &&
                slot.mode == PollCommitMode::Hold &&
                slot.activeHeldEmitter == HeldEmitterSource::KeyboardMouse &&
                !slot.token.active) {
                return false;
            }

            return slot.state != ExecState::Idle ||
                slot.token.active ||
                slot.pending.kind != PendingKind::None ||
                slot.pending.pendingNextPulse ||
                slot.heldContributorMask != 0;
        }

        const char* TraceTagForAction(std::string_view actionId)
        {
            if (actionId == actions::MenuConfirm) {
                return "MenuConfirmTrace";
            }
            if (actionId == actions::Favorites) {
                return "GameFavoritesTrace";
            }
            return nullptr;
        }

        void LogTrackedNativeActionTrace(
            const char* traceTag,
            std::string_view stage,
            const PlannedAction& action,
            bool routeActive)
        {
            const auto committed = presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
            const auto resolved = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
            logger::info(
                "[DualPad][{}] stage={} action={} phase={} contract={} digitalPolicy={} gateAware={} actionContext={} actionEpoch={} frameRouteActive={} presentationEpoch={} presentationDirty=0x{:02X} presentationUiContext={} eligibility={} owner={} navigationOwner={} cursorOwner={} presentationContextRevision={} gameplayPresentationRevision={} resolverHost={} resolverUiContext={} resolverContextRevision={} legacyContext={} legacyEpoch={} menuStackRevision={} topMenuInstancePresent={} topMenuInstance={} observer={} identity={} degraded={}",
                traceTag,
                stage,
                action.actionId.c_str(),
                ToString(action.phase),
                ToString(action.contract),
                ToString(action.digitalPolicy),
                action.gateAware,
                dualpad::input::ToString(action.context),
                action.contextEpoch,
                routeActive,
                committed.epoch,
                ToTraceDirtyBits(committed.dirty),
                static_cast<std::uint16_t>(committed.uiContextId),
                ToTraceString(committed.menuRefreshEligibility),
                ToTraceString(committed.owner),
                ToTraceString(committed.navigationOwner),
                ToTraceString(committed.cursorOwner),
                committed.contextRevision,
                committed.gameplayPresentationRevision,
                ToTraceString(resolved.hostMode),
                static_cast<std::uint16_t>(resolved.uiContextId),
                resolved.contextRevision,
                dualpad::input::ToString(resolved.legacyInputContext),
                resolved.legacyContextEpoch,
                resolved.menuStackRevision,
                resolved.topMenuInstanceId.has_value(),
                resolved.topMenuInstanceId.value_or(0),
                ToTraceString(resolved.menuObserverCompleteness),
                ToTraceString(resolved.identityQuality),
                resolved.menuIdentityDegraded);
        }

        void LogTrackedNativeSlotTrace(
            const char* traceTag,
            std::uint64_t pollSequence,
            const PollCommitSlot& slot,
            std::uint32_t buttonBit)
        {
            const auto committed = presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
            const auto resolved = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
            logger::info(
                "[DualPad][{}] stage=slot poll={} slotAction={} slotContext={} slotEpoch={} outputCode={} bit=0x{:08X} execState={} commitMode={} pending={} nextPulse={} managed={} down={} gateAware={} token={} downSubmitted={} releaseSubmitted={} downCount={} upCount={} coalesced={} dropped={} presentationEpoch={} presentationDirty=0x{:02X} presentationUiContext={} eligibility={} owner={} navigationOwner={} cursorOwner={} resolverHost={} resolverUiContext={} resolverContextRevision={} legacyContext={} legacyEpoch={} menuStackRevision={} topMenuInstancePresent={} topMenuInstance={} observer={} identity={} degraded={}",
                traceTag,
                pollSequence,
                slot.actionId.c_str(),
                dualpad::input::ToString(slot.context),
                slot.epoch,
                ToString(slot.outputCode),
                buttonBit,
                ToString(slot.state),
                ToString(slot.mode),
                ToString(slot.pending.kind),
                slot.pending.pendingNextPulse,
                TraceSlotIsManaged(slot),
                TraceSlotIsDown(slot),
                slot.gateAware,
                slot.token.tokenId,
                slot.token.downSubmitted,
                slot.token.releaseSubmitted,
                slot.emittedDownCount,
                slot.emittedUpCount,
                slot.coalescedPulseCount,
                slot.droppedPulseCount,
                committed.epoch,
                ToTraceDirtyBits(committed.dirty),
                static_cast<std::uint16_t>(committed.uiContextId),
                ToTraceString(committed.menuRefreshEligibility),
                ToTraceString(committed.owner),
                ToTraceString(committed.navigationOwner),
                ToTraceString(committed.cursorOwner),
                ToTraceString(resolved.hostMode),
                static_cast<std::uint16_t>(resolved.uiContextId),
                resolved.contextRevision,
                dualpad::input::ToString(resolved.legacyInputContext),
                resolved.legacyContextEpoch,
                resolved.menuStackRevision,
                resolved.topMenuInstanceId.has_value(),
                resolved.topMenuInstanceId.value_or(0),
                ToTraceString(resolved.menuObserverCompleteness),
                ToTraceString(resolved.identityQuality),
                resolved.menuIdentityDegraded);
        }

        void LogTrackedNativePollTrace(
            const char* traceTag,
            const CommittedButtonState& state,
            std::uint32_t trackedBit,
            std::uint16_t xinputButtons)
        {
            const auto committed = presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
            const auto resolved = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
            logger::info(
                "[DualPad][{}] stage=poll poll={} context={} epoch={} trackedBit=0x{:08X} down=0x{:08X} pressed=0x{:08X} released=0x{:08X} managed=0x{:08X} xinputButtons=0x{:04X} presentationEpoch={} presentationDirty=0x{:02X} presentationUiContext={} eligibility={} owner={} navigationOwner={} cursorOwner={} presentationContextRevision={} gameplayPresentationRevision={} resolverHost={} resolverUiContext={} resolverContextRevision={} legacyContext={} legacyEpoch={} menuStackRevision={} topMenuInstancePresent={} topMenuInstance={} observer={} identity={} degraded={}",
                traceTag,
                state.pollSequence,
                dualpad::input::ToString(state.context),
                state.contextEpoch,
                trackedBit,
                state.buttonDownMask,
                state.buttonPressedMask,
                state.buttonReleasedMask,
                state.managedMask,
                xinputButtons,
                committed.epoch,
                ToTraceDirtyBits(committed.dirty),
                static_cast<std::uint16_t>(committed.uiContextId),
                ToTraceString(committed.menuRefreshEligibility),
                ToTraceString(committed.owner),
                ToTraceString(committed.navigationOwner),
                ToTraceString(committed.cursorOwner),
                committed.contextRevision,
                committed.gameplayPresentationRevision,
                ToTraceString(resolved.hostMode),
                static_cast<std::uint16_t>(resolved.uiContextId),
                resolved.contextRevision,
                dualpad::input::ToString(resolved.legacyInputContext),
                resolved.legacyContextEpoch,
                resolved.menuStackRevision,
                resolved.topMenuInstanceId.has_value(),
                resolved.topMenuInstanceId.value_or(0),
                ToTraceString(resolved.menuObserverCompleteness),
                ToTraceString(resolved.identityQuality),
                resolved.menuIdentityDegraded);
        }

        bool IsGameplayDigitalSuppressionCandidate(const PlannedAction& action)
        {
            if (action.context != InputContext::Gameplay) {
                return false;
            }
            if (!action.gateAware ||
                action.backend != PlannedBackend::NativeButtonCommit ||
                action.kind != PlannedActionKind::NativeButton) {
                return false;
            }

            switch (action.digitalPolicy) {
            case NativeDigitalPolicyKind::DeferredPulse:
            case NativeDigitalPolicyKind::PulseMinDown:
                return action.phase == PlannedActionPhase::Pulse ||
                    action.phase == PlannedActionPhase::Press;
            case NativeDigitalPolicyKind::ToggleDebounced:
                return action.phase == PlannedActionPhase::Pulse ||
                    action.phase == PlannedActionPhase::Press;
            case NativeDigitalPolicyKind::HoldOwner:
            case NativeDigitalPolicyKind::RepeatOwner:
                // Hold/repeat actions need per-action handoff semantics.
                // Suppressing them at the whole-family DigitalOwner layer
                // breaks ownership transitions such as Sprint.
                return false;
            case NativeDigitalPolicyKind::None:
            default:
                return false;
            }
        }
    }

    NativeButtonCommitBackend& NativeButtonCommitBackend::GetSingleton()
    {
        static NativeButtonCommitBackend instance;
        return instance;
    }

    void NativeButtonCommitBackend::Reset()
    {
        std::scoped_lock lock(_lock);
        _pollCommit.Reset();
        _frameContext = InputContext::Gameplay;
        _frameContextEpoch = 0;
        _pollSequence = 0;
        _lastCommittedButtonDownMask = 0;
        _lastSprintProbeSnapshot = {};
        _lastSneakProbeSnapshot = {};
        _suppressGameplayDigitalTransientActions = false;
    }

    bool NativeButtonCommitBackend::IsRouteActive() const
    {
        return UpstreamGamepadHook::GetSingleton().IsRouteActive();
    }

    bool NativeButtonCommitBackend::CanHandleAction(std::string_view actionId) const
    {
        const auto* descriptor = FindNativeActionDescriptor(actionId);
        return descriptor != nullptr &&
            descriptor->backend == PlannedBackend::NativeButtonCommit &&
            descriptor->kind == PlannedActionKind::NativeButton &&
            descriptor->nativeCode != NativeControlCode::None &&
            ResolveVirtualPadBitMask(descriptor->virtualButtonRoles, GetPadBits(GetActivePadProfile())) != 0;
    }

    bool NativeButtonCommitBackend::IsActionDown(std::string_view actionId) const
    {
        std::scoped_lock lock(_lock);
        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }
            if (slot.actionId.c_str() == actionId) {
                return SlotIsDown(slot);
            }
        }
        return false;
    }

    bool NativeButtonCommitBackend::HasHeldContributor(std::string_view actionId, HeldContributor contributor) const
    {
        const auto mask = static_cast<std::uint8_t>(contributor);
        if (mask == 0) {
            return false;
        }

        std::scoped_lock lock(_lock);
        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }
            if (slot.actionId.c_str() == actionId) {
                return (slot.heldContributorMask & mask) != 0;
            }
        }
        return false;
    }

    HeldEmitterSource NativeButtonCommitBackend::GetHeldEmitter(std::string_view actionId) const
    {
        std::scoped_lock lock(_lock);
        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }
            if (slot.actionId.c_str() == actionId) {
                return slot.activeHeldEmitter;
            }
        }
        return HeldEmitterSource::None;
    }

    void NativeButtonCommitBackend::BeginFrame(
        InputContext context,
        std::uint32_t contextEpoch,
        std::uint64_t nowUs)
    {
        std::scoped_lock lock(_lock);
        _frameContext = context;
        _frameContextEpoch = contextEpoch;
        _suppressGameplayDigitalTransientActions = false;
        _pollCommit.BeginFrame(context, contextEpoch, nowUs != 0 ? nowUs : NowUs());
        SyncExternalHeldContributors(context, contextEpoch);
    }

    void NativeButtonCommitBackend::SetGameplayDigitalGatePlan(bool suppressNewTransientActions)
    {
        std::scoped_lock lock(_lock);
        _suppressGameplayDigitalTransientActions = suppressNewTransientActions;
    }

    bool NativeButtonCommitBackend::ApplyPlannedAction(const PlannedAction& action)
    {
        std::scoped_lock lock(_lock);
        const auto routeActive = IsRouteActive();
        const char* traceTag = TraceTagForAction(action.actionId);
        if (traceTag != nullptr) {
            LogTrackedNativeActionTrace(traceTag, "apply", action, routeActive);
        }

        if (IsGameplayDigitalSuppressionCandidate(action) &&
            _suppressGameplayDigitalTransientActions) {
            if (ShouldLogPollCommit()) {
                logger::info(
                    "[DualPad][NativeButtonCommit] Suppressed gameplay digital action={} phase={} via frame digital gate plan",
                    action.actionId.c_str(),
                    ToString(action.phase));
            }
            return true;
        }

        if (ShouldLogPollCommit() &&
            action.actionId == actions::Sneak) {
            logger::info(
                "[DualPad][SneakProbe] apply phase={} contract={} digitalPolicy={} lifecycle={} gateAware={} context={} epoch={}",
                ToString(action.phase),
                ToString(action.contract),
                ToString(action.digitalPolicy),
                ToString(action.lifecyclePolicy),
                action.gateAware,
                dualpad::input::ToString(action.context),
                action.contextEpoch);
        }

        if (ShouldLogPollCommit()) {
            logger::info(
                "[DualPad][NativeButtonCommit] apply action={} phase={} contract={} digitalPolicy={} gateAware={} actionContext={} actionEpoch={} frameContext={} frameEpoch={} outputCode={}",
                action.actionId.c_str(),
                ToString(action.phase),
                ToString(action.contract),
                ToString(action.digitalPolicy),
                action.gateAware,
                dualpad::input::ToString(action.context),
                action.contextEpoch,
                dualpad::input::ToString(_frameContext),
                _frameContextEpoch,
                ToString(static_cast<NativeControlCode>(action.outputCode)));
        }

        PollCommitRequest request{};
        const auto translationResult = TranslatePlannedActionToCommitRequest(action, request);
        if (traceTag != nullptr) {
            logger::info(
                "[DualPad][{}] stage=translate action={} result={} requestKind={} mode={} outputCode={} requestEpoch={}",
                traceTag,
                action.actionId.c_str(),
                ToTraceString(translationResult),
                ToString(request.kind),
                ToString(request.mode),
                ToString(static_cast<NativeControlCode>(action.outputCode)),
                request.epoch);
        }

        if (translationResult == NativeButtonCommitTranslationKind::Noop) {
            if (ShouldLogPollCommit()) {
                logger::debug(
                    "[DualPad][NativeButtonCommit] pulse_release_noop action={} phase={} contract={} digitalPolicy={} outputCode={}",
                    action.actionId.c_str(),
                    ToString(action.phase),
                    ToString(action.contract),
                    ToString(action.digitalPolicy),
                    ToString(static_cast<NativeControlCode>(action.outputCode)));
            }
            return true;
        }

        if (translationResult == NativeButtonCommitTranslationKind::Invalid) {
            if (ShouldLogPollCommit()) {
                logger::warn(
                    "[DualPad][NativeButtonCommit] translate_failed action={} phase={} contract={} digitalPolicy={} outputCode={}",
                    action.actionId.c_str(),
                    ToString(action.phase),
                    ToString(action.contract),
                    ToString(action.digitalPolicy),
                    ToString(static_cast<NativeControlCode>(action.outputCode)));
            }
            return false;
        }

        const auto queued = _pollCommit.QueueRequest(request);
        if (traceTag != nullptr) {
            logger::info(
                "[DualPad][{}] stage=queue action={} queued={} requestKind={} mode={} outputCode={} requestEpoch={} routeActive={}",
                traceTag,
                request.actionId.c_str(),
                queued,
                ToString(request.kind),
                ToString(request.mode),
                ToString(request.outputCode),
                request.epoch,
                routeActive);
        }

        if (ShouldLogPollCommit()) {
            logger::info(
                "[DualPad][NativeButtonCommit] queue action={} kind={} mode={} queued={} requestEpoch={} gateAware={}",
                request.actionId.c_str(),
                ToString(request.kind),
                ToString(request.mode),
                queued,
                request.epoch,
                request.gateAware);
        }
        return queued;
    }

    void NativeButtonCommitBackend::ForceCancelGateAwareGameplayTransientActions()
    {
        std::scoped_lock lock(_lock);
        _pollCommit.ForceCancelGateAwareTransientSlots();
    }

    CommittedButtonState NativeButtonCommitBackend::CommitPollState()
    {
        if (!IsRouteActive()) {
            return {};
        }

        std::scoped_lock lock(_lock);

        const auto nowUs = NowUs();
        const auto contextSnapshot = dualpad::input_v2::context::ContextResolver::GetSingleton().GetPublishedSnapshot();
        const auto context = contextSnapshot.legacyInputContext;
        const auto contextEpoch = contextSnapshot.legacyContextEpoch;
        _frameContext = context;
        _frameContextEpoch = contextEpoch;

        _pollCommit.BeginFrame(context, contextEpoch, nowUs);
        SyncExternalHeldContributors(context, contextEpoch);
        _pollCommit.Tick(nowUs, IsGameplayGateOpen(context));
        _pollCommit.Flush(*this, nowUs);

        SprintProbeSnapshot sprintSnapshot{};
        sprintSnapshot.valid = true;
        sprintSnapshot.kbmHeld =
            context == InputContext::Gameplay &&
            GameplayKbmFactTracker::GetSingleton().GetFacts().IsKeyboardMouseSprintActive();
        sprintSnapshot.gameplayOwnerGamepad =
            dualpad::input_v2::gameplay::DualPadRuntime::GetSingleton().GetPublishedGameplayPresentation().engineOwner ==
            dualpad::input_v2::presentation::PresentationOwner::Gamepad;
        sprintSnapshot.context = context;
        sprintSnapshot.contextEpoch = contextEpoch;

        CommittedButtonState result{};
        result.context = context;
        result.contextEpoch = contextEpoch;
        result.pollSequence = ++_pollSequence;
        const auto favoritesBit = ToVirtualPadBit(NativeControlCode::FavoritesCombo);
        bool hasGameFavoritesSlotState = false;
        SneakProbeSnapshot sneakSnapshot{};
        sneakSnapshot.valid = true;
        sneakSnapshot.context = context;
        sneakSnapshot.contextEpoch = contextEpoch;
        sneakSnapshot.pollSequence = result.pollSequence;

        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }

            const auto buttonBit = ToVirtualPadBit(slot.outputCode);
            if (buttonBit == 0) {
                continue;
            }

            if (SlotIsManaged(slot)) {
                result.managedMask |= buttonBit;
            }
            if (SlotIsDown(slot)) {
                result.buttonDownMask |= buttonBit;
            }

            if (slot.actionId == RE::BSFixedString(actions::Favorites.data()) &&
                TraceSlotIsManaged(slot)) {
                hasGameFavoritesSlotState = true;
                LogTrackedNativeSlotTrace(
                    "GameFavoritesTrace",
                    result.pollSequence,
                    slot,
                    buttonBit);
            }

            if (ShouldLogPollCommit() && SlotIsManaged(slot)) {
                logger::info(
                    "[DualPad][NativeButtonCommit] poll={} context={} action={} code={} execState={} commitMode={} managed={} down={} epoch={} token={} nextPulse={} desiredHeld={} downCount={} upCount={}",
                    result.pollSequence,
                    dualpad::input::ToString(slot.context),
                    slot.actionId.c_str(),
                    ToString(slot.outputCode),
                    ToString(slot.state),
                    ToString(slot.mode),
                    SlotIsManaged(slot),
                    SlotIsDown(slot),
                    slot.epoch,
                    slot.token.tokenId,
                    slot.pending.pendingNextPulse,
                    slot.actionId == RE::BSFixedString(actions::Sprint.data()) ?
                        slot.activeHeldEmitter == HeldEmitterSource::Gamepad :
                        slot.heldContributorMask != 0,
                    slot.emittedDownCount,
                    slot.emittedUpCount);
            }

            if (slot.actionId == RE::BSFixedString(actions::Sprint.data())) {
                sprintSnapshot.gamepadContributor =
                    (slot.heldContributorMask & static_cast<std::uint8_t>(HeldContributor::Gamepad)) != 0;
                sprintSnapshot.keyboardMouseContributor =
                    (slot.heldContributorMask & static_cast<std::uint8_t>(HeldContributor::KeyboardMouse)) != 0;
                sprintSnapshot.effectiveHeld = slot.heldContributorMask != 0;
                sprintSnapshot.actionDown = SlotIsDown(slot);
                sprintSnapshot.managed = SlotIsManaged(slot);
                sprintSnapshot.state = slot.state;
                sprintSnapshot.activeEmitter = slot.activeHeldEmitter;
            }

            if (slot.actionId == RE::BSFixedString(actions::Sneak.data())) {
                sneakSnapshot.actionDown = SlotIsDown(slot);
                sneakSnapshot.managed = SlotIsManaged(slot);
                sneakSnapshot.gateAware = slot.gateAware;
                sneakSnapshot.state = slot.state;
                sneakSnapshot.mode = slot.mode;
            }
        }

        if (ShouldLogPollCommit()) {
            const auto changed =
                !_lastSprintProbeSnapshot.valid ||
                sprintSnapshot.kbmHeld != _lastSprintProbeSnapshot.kbmHeld ||
                sprintSnapshot.gamepadContributor != _lastSprintProbeSnapshot.gamepadContributor ||
                sprintSnapshot.keyboardMouseContributor != _lastSprintProbeSnapshot.keyboardMouseContributor ||
                sprintSnapshot.effectiveHeld != _lastSprintProbeSnapshot.effectiveHeld ||
                sprintSnapshot.actionDown != _lastSprintProbeSnapshot.actionDown ||
                sprintSnapshot.managed != _lastSprintProbeSnapshot.managed ||
                sprintSnapshot.gameplayOwnerGamepad != _lastSprintProbeSnapshot.gameplayOwnerGamepad ||
                sprintSnapshot.state != _lastSprintProbeSnapshot.state ||
                sprintSnapshot.activeEmitter != _lastSprintProbeSnapshot.activeEmitter ||
                sprintSnapshot.context != _lastSprintProbeSnapshot.context ||
                sprintSnapshot.contextEpoch != _lastSprintProbeSnapshot.contextEpoch;
            if (changed) {
                logger::info(
                    "[DualPad][SprintProbe] snapshot poll={} ctx={} epoch={} kbmHeld={} gpContributor={} kbmContributor={} effectiveHeld={} actionDown={} managed={} activeEmitter={} gameplayOwnerGamepad={} state={}",
                    result.pollSequence,
                    dualpad::input::ToString(sprintSnapshot.context),
                    sprintSnapshot.contextEpoch,
                    sprintSnapshot.kbmHeld,
                    sprintSnapshot.gamepadContributor,
                    sprintSnapshot.keyboardMouseContributor,
                    sprintSnapshot.effectiveHeld,
                    sprintSnapshot.actionDown,
                    sprintSnapshot.managed,
                    ToString(sprintSnapshot.activeEmitter),
                    sprintSnapshot.gameplayOwnerGamepad,
                    ToString(sprintSnapshot.state));
                _lastSprintProbeSnapshot = sprintSnapshot;
            }
        }

        if (ShouldLogPollCommit()) {
            const auto changed =
                !_lastSneakProbeSnapshot.valid ||
                sneakSnapshot.actionDown != _lastSneakProbeSnapshot.actionDown ||
                sneakSnapshot.managed != _lastSneakProbeSnapshot.managed ||
                sneakSnapshot.gateAware != _lastSneakProbeSnapshot.gateAware ||
                sneakSnapshot.state != _lastSneakProbeSnapshot.state ||
                sneakSnapshot.mode != _lastSneakProbeSnapshot.mode ||
                sneakSnapshot.context != _lastSneakProbeSnapshot.context ||
                sneakSnapshot.contextEpoch != _lastSneakProbeSnapshot.contextEpoch;
            if (changed) {
                logger::info(
                    "[DualPad][SneakProbe] snapshot poll={} ctx={} epoch={} actionDown={} managed={} gateAware={} mode={} state={}",
                    sneakSnapshot.pollSequence,
                    dualpad::input::ToString(sneakSnapshot.context),
                    sneakSnapshot.contextEpoch,
                    sneakSnapshot.actionDown,
                    sneakSnapshot.managed,
                    sneakSnapshot.gateAware,
                    ToString(sneakSnapshot.mode),
                    ToString(sneakSnapshot.state));
                _lastSneakProbeSnapshot = sneakSnapshot;
            }
        }

        result.buttonPressedMask = result.buttonDownMask & ~_lastCommittedButtonDownMask;
        result.buttonReleasedMask = _lastCommittedButtonDownMask & ~result.buttonDownMask;
        _lastCommittedButtonDownMask = result.buttonDownMask;

        const auto confirmBit = ToVirtualPadBit(NativeControlCode::MenuConfirm);
        const bool hasMenuConfirmState = confirmBit != 0 &&
            ((result.buttonDownMask |
              result.buttonPressedMask |
              result.buttonReleasedMask |
              result.managedMask) &
             confirmBit) != 0;
        if (hasMenuConfirmState) {
            LogTrackedNativePollTrace(
                "MenuConfirmTrace",
                result,
                confirmBit,
                dualpad::input::ToXInputButtons(result.buttonDownMask));
        }

        const bool hasDpadUpState = favoritesBit != 0 &&
            ((result.buttonDownMask |
              result.buttonPressedMask |
              result.buttonReleasedMask |
              result.managedMask) &
             favoritesBit) != 0;
        if (hasDpadUpState || hasGameFavoritesSlotState) {
            LogTrackedNativePollTrace(
                hasGameFavoritesSlotState ? "GameFavoritesTrace" : "DpadUpTrace",
                result,
                favoritesBit,
                dualpad::input::ToXInputButtons(result.buttonDownMask));
        }

        AuthoritativePollState::GetSingleton().PublishCommittedButtons(
            result.buttonDownMask,
            result.buttonPressedMask,
            result.buttonReleasedMask,
            result.managedMask,
            result.pollSequence,
            result.context,
            result.contextEpoch);

        return result;
    }

    EmitResult NativeButtonCommitBackend::Emit(const EmitRequest& request)
    {
        const auto decision = ActionBackendPolicy::Decide(request.actionId);
        if (decision.backend != PlannedBackend::NativeButtonCommit ||
            decision.kind != PlannedActionKind::NativeButton ||
            decision.nativeCode == NativeControlCode::None ||
            ToVirtualPadBit(decision.nativeCode) == 0) {
            return {};
        }

        if (ShouldLogPollCommit()) {
            logger::info(
                "[DualPad][NativeButtonCommit] emit action={} edge={} epoch={} token={} context={} held={:.3f}",
                request.actionId.c_str(),
                request.edge == EmitEdge::Down ? "Down" : "Up",
                request.epoch,
                request.tokenId,
                dualpad::input::ToString(request.context),
                request.heldSeconds);
        }

        // In the poll-owned mainline this is a commit-FSM acknowledgement, not
        // direct BSInputEvent queue injection. Gameplay-visible state is
        // materialized later by CommitPollState() exporting the committed
        // virtual button current-state.
        return {
            .submitted = true,
            .queueFull = false,
            .transientBlocked = false
        };
    }

    NativeButtonCommitTranslationKind NativeButtonCommitBackend::TranslatePlannedActionToCommitRequest(
        const PlannedAction& action,
        PollCommitRequest& outRequest)
    {
        const auto translation = TranslatePlannedActionForNativeButtonCommit(action);
        if (translation.kind != NativeButtonCommitTranslationKind::Request) {
            return translation.kind;
        }

        outRequest.actionId = RE::BSFixedString(action.actionId.c_str());
        outRequest.context = action.context;
        outRequest.outputCode = static_cast<NativeControlCode>(action.outputCode);
        outRequest.mode = translation.mode;
        outRequest.kind = translation.requestKind;
        outRequest.contributor = translation.contributor;
        outRequest.gateAware = action.gateAware;
        outRequest.epoch = action.contextEpoch;
        outRequest.timestampUs = action.timestampUs;
        outRequest.minDownMs = action.minDownMs;
        outRequest.repeatDelayMs = action.repeatDelayMs;
        outRequest.repeatIntervalMs = action.repeatIntervalMs;

        return NativeButtonCommitTranslationKind::Request;
    }

    std::uint64_t NativeButtonCommitBackend::NowUs()
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    }

    std::uint32_t NativeButtonCommitBackend::ToVirtualPadBit(NativeControlCode code)
    {
        return ResolveVirtualPadBitMask(code, GetPadBits(GetActivePadProfile()));
    }

    bool NativeButtonCommitBackend::ShouldLogPollCommit()
    {
        const auto& config = RuntimeConfig::GetSingleton();
        return config.LogActionPlan() || config.LogNativeInjection();
    }

    bool NativeButtonCommitBackend::IsGameplayGateOpen(InputContext context)
    {
        return IsNativeDigitalGateOpenForContext(context);
    }

    bool NativeButtonCommitBackend::SlotIsDown(const PollCommitSlot& slot)
    {
        return slot.token.active &&
            slot.token.downSubmitted &&
            !slot.token.releaseSubmitted;
    }

    bool NativeButtonCommitBackend::SlotIsManaged(const PollCommitSlot& slot)
    {
        if (slot.actionId == RE::BSFixedString(actions::Sprint.data()) &&
            slot.mode == PollCommitMode::Hold &&
            slot.activeHeldEmitter == HeldEmitterSource::KeyboardMouse &&
            !slot.token.active) {
            return false;
        }

        return slot.state != ExecState::Idle ||
            slot.token.active ||
            slot.pending.kind != PendingKind::None ||
            slot.pending.pendingNextPulse ||
            slot.heldContributorMask != 0;
    }

    void NativeButtonCommitBackend::SyncExternalHeldContributors(InputContext context, std::uint32_t)
    {
        constexpr bool kbmSprintHeld = false;
        if (ShouldLogPollCommit()) {
            logger::info(
                "[DualPad][SprintProbe] SyncExternalHeldContributors kbmSprintHeld={} ctx={}",
                kbmSprintHeld,
                dualpad::input::ToString(context));
        }
        _pollCommit.SyncHeldContributor(
            RE::BSFixedString(actions::Sprint.data()),
            HeldContributor::KeyboardMouse,
            kbmSprintHeld);
    }
}
