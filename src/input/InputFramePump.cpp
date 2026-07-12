#include "pch.h"
#include "input/InputFramePump.h"

#include "input/HidReader.h"
#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
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
#include "input_v2/runtime/RuntimeOwnerGuard.h"

#include <algorithm>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::uint64_t kUpstreamPollAssistWindowMs = 250;

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
        const auto contextSnapshot =
            input_v2::context::ContextResolver::GetSingleton().GetPublishedSnapshot();
        const auto bindings = _skyrimKbmAdapter.CaptureBindingSnapshot(contextSnapshot);
        const auto observed = _skyrimKbmAdapter.ObserveEventList(
            event,
            bindings,
            frameToken,
            eventBatchToken,
            ownerNowUs);
        bool kbmBatchAccepted = false;
        if (bindings.complete && observed.eventListComplete) {
            auto kbmBatch = _kbmProducer.BuildIngressBatch(
                observed,
                bindings,
                contextSnapshot,
                ownerNowUs);
            const auto receipt = input_v2::ingress::IngressHub::GetSingleton().PublishOwnerKbmBatch(
                input_v2::ingress::OwnerKbmIngressDraft{
                    .boundary = input_v2::ingress::IngressBoundaryObservation{
                        .contextRevision = contextSnapshot.contextRevision,
                        .menuStackRevision = contextSnapshot.menuStackRevision,
                        .controlMapFingerprint = bindings.controlMapFingerprint,
                        .bindingGeneration = bindings.generation
                    },
                    .kbm = std::move(kbmBatch)
                });
            kbmBatchAccepted = receipt.accepted;
        }
        const auto currentCyclePlan = input_v2::gameplay::BuildCurrentCycleGatePlan(
            BuildCurrentCycleInput(consumedReceipt, observed, bindings, kbmBatchAccepted));
        const auto preparedCurrentCycle =
            input_v2::gameplay::RuntimeInputPublication::GetSingleton()
                .PrepareCallbackAudit(frameToken, currentCyclePlan);
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

