#include "pch.h"
#include "input/InputFramePump.h"

#include "input/HidReader.h"
#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/KbmIngressDiagnostics.h"
#include "input/injection/PollMaterializationReceipt.h"
#include "input/injection/RouteHealthContract.h"
#include "input/injection/SkyrimKbmInputAdapter.h"
#include "input/injection/UpstreamGamepadHook.h"
#include "input_v2/context/ContextRefreshTick.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/ingress/KbmGameplayFactProducer.h"
#include "input_v2/gameplay/CurrentCycleGatePlan.h"
#include "input_v2/gameplay/RuntimeInputPublication.h"
#include "input_v2/runtime/InputRecovery.h"
#include "input_v2/runtime/RuntimeOwnerGuard.h"

#include <algorithm>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::uint64_t kUpstreamPollAssistWindowMs = 250;
        KbmIngressDiagnosticSampler g_kbmIngressSampler{ 5'000 };

        std::uint64_t NowMonotonicUs()
        {
            return ::GetTickCount64() * 1000;
        }

        input_v2::gameplay::CurrentCycleGateInput BuildCurrentCycleInput(
            const PollReceiptConsumeResult& consumedReceipt,
            const input_v2::ingress::KbmObservedBatch& observed,
            const input_v2::ingress::KbmBindingSnapshot& bindings,
            bool kbmBatchAccepted)
        {
            using namespace input_v2;
            gameplay::CurrentCycleGateInput input{
                .receipt = consumedReceipt.receipt,
                .receiptFailure = consumedReceipt.failure,
                .observedIdentity = consumedReceipt.receipt ?
                    consumedReceipt.receipt->identity : PollFrameIdentity{},
                .physicalFactsComplete = bindings.complete &&
                    observed.eventListComplete &&
                    kbmBatchAccepted,
                .routeAvailable = consumedReceipt.receipt && consumedReceipt.receipt->routeAvailable,
                .consumerOrderProven = false,
                .mutationCapabilityEnabled = SkyrimCurrentCycleEventAdapter::ProductionMutationEnabled(),
                .scratchCapacitySufficient = true,
                .materializedLookEvent = consumedReceipt.receipt &&
                    consumedReceipt.receipt->materializedLookEvent,
                .materializedMoveEvent = consumedReceipt.receipt &&
                    consumedReceipt.receipt->materializedMoveEvent,
                .materializedCombatEvent = consumedReceipt.receipt &&
                    consumedReceipt.receipt->materializedCombatEvent,
                .materializedTransientEvent = consumedReceipt.receipt &&
                    consumedReceipt.receipt->materializedTransientEvent
            };

            for (const auto& event : observed.events) {
                if (event.phase == ingress::KbmEdgePhase::MouseDelta) {
                    input.physicalLookActivation = true;
                    continue;
                }
                if (event.phase != ingress::KbmEdgePhase::Press || !event.initialPress) {
                    continue;
                }
                const auto binding = std::find_if(
                    bindings.entries.begin(),
                    bindings.entries.end(),
                    [&](const ingress::KbmBindingEntry& entry) {
                        return entry.physical == event.physical;
                    });
                if (binding == bindings.entries.end()) {
                    continue;
                }
                switch (binding->gameplayClass) {
                case ingress::KbmGameplayClass::Look:
                    input.physicalLookActivation = true;
                    break;
                case ingress::KbmGameplayClass::Move:
                    input.physicalMoveActivation = true;
                    break;
                case ingress::KbmGameplayClass::Combat:
                    input.physicalCombatActivation = true;
                    break;
                case ingress::KbmGameplayClass::TransientDigital:
                    input.physicalTransientActivation = true;
                    break;
                case ingress::KbmGameplayClass::SustainedDigital:
                default:
                    break;
                }
            }
            return input;
        }

    }

    InputFramePump& InputFramePump::GetSingleton()
    {
        static InputFramePump instance;
        return instance;
    }

    std::uint64_t InputFramePump::NextEventBatchToken()
    {
        return ++_eventBatchToken;
    }

    void InputFramePump::Register()
    {
        if (_registered) {
            return;
        }

        auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (!inputManager) {
            logger::warn("[DualPad][FramePump] Failed to get BSInputDeviceManager, using task fallback");
            PadEventSnapshotDispatcher::GetSingleton().SetFramePumpEnabled(false);
            return;
        }

        inputManager->PrependEventSink(this);
        _registered = true;
        PadEventSnapshotDispatcher::GetSingleton().SetFramePumpEnabled(true);
        logger::info("[DualPad][FramePump] Registered on BSInputDeviceManager input pump");
    }

    void InputFramePump::Unregister()
    {
        input_v2::runtime::InputRecoveryMailbox::GetSingleton().Reset();
        if (!_registered) {
            return;
        }

        if (auto* inputManager = RE::BSInputDeviceManager::GetSingleton(); inputManager) {
            inputManager->RemoveEventSink(this);
        }

        _registered = false;
        PadEventSnapshotDispatcher::GetSingleton().SetFramePumpEnabled(false);
        PollMaterializationReceiptStore::GetSingleton().Reset();
        input_v2::runtime::RuntimeOwnerGuard::GetSingleton().Stop();
        logger::info("[DualPad][FramePump] Unregistered from BSInputDeviceManager input pump");
    }

    RE::BSEventNotifyControl InputFramePump::ProcessEvent(
        RE::InputEvent* const* event,
        RE::BSTEventSource<RE::InputEvent*>* source)
    {
        (void)source;

        const auto frameToken = input_v2::context::ContextRefreshTick::GetSingleton().BeginFrame();
        const auto consumedReceipt = PollMaterializationReceiptStore::GetSingleton()
            .ConsumeForThread(::GetCurrentThreadId());
        const auto eventBatchToken = NextEventBatchToken();
        const auto ownerNowUs = NowMonotonicUs();
        for (const auto& recovery :
            input_v2::runtime::InputRecoveryMailbox::GetSingleton().ConsumeAll()) {
            if (recovery.quarantineKeyboardMouse) {
                _kbmProducer.EnterQuarantine(
                    recovery.reasons,
                    recovery.contextRevision,
                    recovery.controlMapRevision);
            }
            if (recovery.resetSyntheticSuppression) {
                _kbmProducer.ResetSyntheticSuppression(recovery.reasons);
            }
        }
        const auto contextSnapshot =
            input_v2::context::ContextResolver::GetSingleton().GetPublishedSnapshot();
        const auto bindings = _skyrimKbmAdapter.CaptureBindingSnapshot(contextSnapshot);
        const auto observed = _skyrimKbmAdapter.ObserveEventList(
            event,
            bindings,
            frameToken,
            eventBatchToken,
            ownerNowUs);
        const char* firstNativeUserEvent = "";
        float firstNativeValue = 0.0F;
        float firstNativeHeldDuration = 0.0F;
        if (!observed.events.empty() && event && *event) {
            const auto targetOrdinal = observed.events.front().eventOrdinal;
            std::uint32_t ordinal = 0;
            for (auto* current = *event; current; current = current->next) {
                if (++ordinal != targetOrdinal) {
                    continue;
                }
                if (const auto* button = current->AsButtonEvent()) {
                    const auto& userEvent = button->GetUserEvent();
                    firstNativeUserEvent = userEvent.c_str() ? userEvent.c_str() : "";
                    firstNativeValue = button->Value();
                    firstNativeHeldDuration = button->HeldDuration();
                }
                break;
            }
        }
        input_v2::ingress::PublishedIngressBatchReceipt kbmReceipt{};
        bool kbmBatchAccepted = false;
        bool kbmBatchBuilt = false;
        std::uint32_t mappedEdgeCount = 0;
        std::uint32_t sourceActivityCount = 0;
        std::uint32_t physicalDownCount = 0;
        std::uint32_t quarantineCount = 0;
        input_v2::ingress::KbmGameplayCurrentFacts kbmCurrent{};
        input_v2::ingress::KbmGameplayClass firstMappedClass =
            input_v2::ingress::KbmGameplayClass::TransientDigital;
        std::string firstMappedAction;
        if (bindings.complete && observed.eventListComplete) {
            auto kbmBatch = _kbmProducer.BuildIngressBatch(
                observed,
                bindings,
                contextSnapshot,
                ownerNowUs);
            kbmBatchBuilt = true;
            mappedEdgeCount = static_cast<std::uint32_t>(kbmBatch.orderedEdges.size());
            sourceActivityCount = static_cast<std::uint32_t>(kbmBatch.sourceActivities.size());
            physicalDownCount = static_cast<std::uint32_t>(kbmBatch.physical.downCodes.count);
            quarantineCount = static_cast<std::uint32_t>(kbmBatch.physical.quarantineCodes.count);
            kbmCurrent = kbmBatch.completeCurrent;
            if (!kbmBatch.orderedEdges.empty()) {
                firstMappedClass = kbmBatch.orderedEdges.front().gameplayClass;
                firstMappedAction = kbmBatch.orderedEdges.front().actionId;
            }
            kbmReceipt = input_v2::ingress::IngressHub::GetSingleton().PublishOwnerKbmBatch(
                input_v2::ingress::OwnerKbmIngressDraft{
                    .boundary = input_v2::ingress::IngressBoundaryObservation{
                        .contextRevision = contextSnapshot.contextRevision,
                        .menuStackRevision = contextSnapshot.menuStackRevision,
                        .controlMapFingerprint = bindings.controlMapFingerprint,
                        .bindingGeneration = bindings.generation
                    },
                    .kbm = std::move(kbmBatch)
                });
            kbmBatchAccepted = kbmReceipt.accepted;
        }
        KbmIngressDiagnosticInput kbmDiagnostic{
            .contextRevision = contextSnapshot.contextRevision,
            .menuStackRevision = contextSnapshot.menuStackRevision,
            .controlMapRevision = kbmReceipt.controlMapRevision,
            .bindingGeneration = bindings.generation,
            .observedEventCount = static_cast<std::uint32_t>(observed.events.size()),
            .mappedEdgeCount = mappedEdgeCount,
            .sourceActivityCount = sourceActivityCount,
            .physicalDownCount = physicalDownCount,
            .quarantineCount = quarantineCount,
            .keyboardMoveHeldMask = kbmCurrent.keyboardMoveHeldMask,
            .keyboardCombatHeldMask = kbmCurrent.keyboardCombatHeldMask,
            .mouseCombatHeldMask = kbmCurrent.mouseCombatHeldMask,
            .keyboardTransientHeldMask = kbmCurrent.keyboardTransientHeldMask,
            .mouseTransientHeldMask = kbmCurrent.mouseTransientHeldMask,
            .keyboardSustainedHeldMask = kbmCurrent.keyboardSustainedHeldMask,
            .mouseSustainedHeldMask = kbmCurrent.mouseSustainedHeldMask,
            .bindingsComplete = bindings.complete,
            .eventListComplete = observed.eventListComplete,
            .batchBuilt = kbmBatchBuilt,
            .batchAccepted = kbmBatchAccepted
        };
        if (!observed.events.empty()) {
            const auto& first = observed.events.front();
            kbmDiagnostic.firstDevice = static_cast<std::uint8_t>(first.physical.device);
            kbmDiagnostic.firstIdCode = first.physical.idCode;
            kbmDiagnostic.firstPhase = static_cast<std::uint8_t>(first.phase);
            kbmDiagnostic.firstInitialPress = first.initialPress;
        }
        const auto kbmFingerprint = BuildKbmIngressFingerprint(kbmDiagnostic);
        const auto kbmSample = g_kbmIngressSampler.Observe(ownerNowUs / 1000, kbmDiagnostic);
        if (kbmSample.record) {
            logger::info(
                "[DualPad][KbmIngressShadow] callbackCount={} sampleReason={} fingerprint=0x{:X} thread={} frameToken={} eventBatchToken={} contextRevision={} menuStackRevision={} bindingGeneration={} bindingsComplete={} eventListComplete={} observedEventCount={} firstDevice={} firstIdCode=0x{:X} firstPhase={} firstInitialPress={} firstNativeUserEvent='{}' firstNativeValue={} firstNativeHeldDuration={} batchBuilt={} mappedEdgeCount={} firstMappedClass={} firstMappedAction='{}' sourceActivityCount={} currentComplete={} keyboardMove=0x{:X} keyboardCombat=0x{:X} mouseCombat=0x{:X} keyboardTransient=0x{:X} mouseTransient=0x{:X} keyboardSustained=0x{:X} mouseSustained=0x{:X} physicalDownCount={} quarantineCount={} batchAccepted={} firstOrderedSeq={} causalTail={} inputStateEpoch={} gamepadSessionId={} controlMapRevision={} productionMutationEnabled=false enginePatchEnabled=false",
                kbmSample.callbackCount,
                ToString(kbmSample.reason),
                kbmFingerprint,
                ::GetCurrentThreadId(),
                frameToken,
                eventBatchToken,
                contextSnapshot.contextRevision,
                contextSnapshot.menuStackRevision,
                bindings.generation,
                bindings.complete,
                observed.eventListComplete,
                observed.events.size(),
                kbmDiagnostic.firstDevice,
                kbmDiagnostic.firstIdCode,
                kbmDiagnostic.firstPhase,
                kbmDiagnostic.firstInitialPress,
                firstNativeUserEvent,
                firstNativeValue,
                firstNativeHeldDuration,
                kbmBatchBuilt,
                mappedEdgeCount,
                static_cast<std::uint8_t>(firstMappedClass),
                firstMappedAction,
                sourceActivityCount,
                kbmCurrent.complete,
                kbmCurrent.keyboardMoveHeldMask,
                kbmCurrent.keyboardCombatHeldMask,
                kbmCurrent.mouseCombatHeldMask,
                kbmCurrent.keyboardTransientHeldMask,
                kbmCurrent.mouseTransientHeldMask,
                kbmCurrent.keyboardSustainedHeldMask,
                kbmCurrent.mouseSustainedHeldMask,
                physicalDownCount,
                quarantineCount,
                kbmBatchAccepted,
                kbmReceipt.firstOrderedSeq,
                kbmReceipt.causalOrderedTailSeq,
                kbmReceipt.inputStateEpoch,
                kbmReceipt.gamepadSessionId,
                kbmReceipt.controlMapRevision);
        }
        const auto currentCyclePlan = input_v2::gameplay::BuildCurrentCycleGatePlan(
            BuildCurrentCycleInput(consumedReceipt, observed, bindings, kbmBatchAccepted));
        const auto preparedCurrentCycle =
            input_v2::gameplay::RuntimeInputPublication::GetSingleton()
                .PrepareCallbackAudit(
                    frameToken,
                    currentCyclePlan,
                    input_v2::gameplay::CurrentCycleCallbackEvidence{
                        .ownerTickToken = frameToken,
                        .monotonicUs = ownerNowUs,
                        .currentInputStateEpoch = kbmReceipt.inputStateEpoch,
                        .currentGamepadSessionId = kbmReceipt.gamepadSessionId,
                        .receiptFailure = consumedReceipt.failure,
                        .receipt = consumedReceipt.receipt });
        const auto currentCycleAudit = _currentCycleAdapter.AuditEventListShadow(
            event,
            currentCyclePlan);
        (void)input_v2::gameplay::RuntimeInputPublication::GetSingleton()
            .CommitCallbackAudit(preparedCurrentCycle.token, currentCycleAudit);

        auto& upstreamHook = UpstreamGamepadHook::GetSingleton();
        if (RuntimeConfig::GetSingleton().UseUpstreamGamepadHook()) {
            if (!upstreamHook.IsInstalled()) {
                upstreamHook.Install();
            }

            if (upstreamHook.IsRouteActive()) {
                if (!IsHidReaderRunning()) {
                    StartHidReader();
                    logger::info("[DualPad][FramePump] Deferred HID reader start released via input pump activity");
                }
            }
        }

        const auto lastPollAgeMs = upstreamHook.GetLastPollCallAgeMs();
        const DrainTelemetryContext telemetry{
            .reason = DrainReason::RuntimeOwnerInputPump,
            .routeState = ResolveUpstreamRouteState(
                upstreamHook.IsRouteActive(),
                lastPollAgeMs,
                kUpstreamPollAssistWindowMs),
            .lastPollAgeMs = lastPollAgeMs,
            .hookInstalled = upstreamHook.IsInstalled()
        };
        PadEventSnapshotDispatcher::GetSingleton().DrainOnOwnerTick(
            PadEventSnapshotDispatcher::DefaultDrainBudget(),
            &telemetry,
            frameToken);
        input_v2::gameplay::RuntimeInputPublication::GetSingleton().ClearCallbackAudit(frameToken);

        return RE::BSEventNotifyControl::kContinue;
    }
}

