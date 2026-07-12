#include "pch.h"
#include "input/InputFramePump.h"

#include "input/HidReader.h"
#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/RouteHealthContract.h"
#include "input/injection/SkyrimKbmInputAdapter.h"
#include "input/injection/UpstreamGamepadHook.h"
#include "input_v2/context/ContextRefreshTick.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/ingress/KbmGameplayFactProducer.h"
#include "input_v2/runtime/RuntimeOwnerGuard.h"

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
        input_v2::runtime::RuntimeOwnerGuard::GetSingleton().Stop();
        logger::info("[DualPad][FramePump] Unregistered from BSInputDeviceManager input pump");
    }

    RE::BSEventNotifyControl InputFramePump::ProcessEvent(
        RE::InputEvent* const* event,
        RE::BSTEventSource<RE::InputEvent*>* source)
    {
        (void)source;

        const auto frameToken = input_v2::context::ContextRefreshTick::GetSingleton().BeginFrame();
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
        if (observed.eventListComplete) {
            auto kbmBatch = _kbmProducer.BuildIngressBatch(
                observed,
                bindings,
                contextSnapshot,
                ownerNowUs);
            (void)input_v2::ingress::IngressHub::GetSingleton().PublishOwnerKbmBatch(
                input_v2::ingress::OwnerKbmIngressDraft{
                    .boundary = input_v2::ingress::IngressBoundaryObservation{
                        .contextRevision = contextSnapshot.contextRevision,
                        .menuStackRevision = contextSnapshot.menuStackRevision,
                        .controlMapFingerprint = bindings.controlMapFingerprint,
                        .bindingGeneration = bindings.generation
                    },
                    .kbm = std::move(kbmBatch)
                });
        }

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

        return RE::BSEventNotifyControl::kContinue;
    }
}

