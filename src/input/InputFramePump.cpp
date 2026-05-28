#include "pch.h"
#include "input/InputFramePump.h"

#include "input/HidReader.h"
#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/RouteHealthContract.h"
#include "input/injection/UpstreamGamepadHook.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/LiveInputFactProducer.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::size_t kUpstreamAssistDrainBudget = 32;
        constexpr std::uint64_t kUpstreamPollAssistWindowMs = 250;

        std::uint64_t NowMonotonicUs()
        {
            return ::GetTickCount64() * 1000;
        }

        void PublishKeyboardMouseEvidence(RE::InputEvent* const* events)
        {
            if (!events || !*events) {
                return;
            }

            auto& producer = input_v2::ingress::LiveInputFactProducer::GetSingleton();
            const auto& contextSnapshot =
                input_v2::context::ContextResolver::GetSingleton().GetPublishedSnapshot();
            for (auto* current = *events; current; current = current->next) {
                const auto tick = NowMonotonicUs();
                switch (current->GetEventType()) {
                case RE::INPUT_EVENT_TYPE::kButton:
                    if (const auto* button = current->AsButtonEvent()) {
                        if (!(button->IsPressed() || button->IsUp())) {
                            break;
                        }

                        if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                            producer.PublishKeyboardSourceEvidence(
                                contextSnapshot,
                                button->GetIDCode(),
                                tick);
                        } else if (button->GetDevice() == RE::INPUT_DEVICE::kMouse) {
                            producer.PublishMouseButtonSourceEvidence(contextSnapshot, tick);
                        }
                    }
                    break;
                case RE::INPUT_EVENT_TYPE::kMouseMove:
                    if (const auto* move = current->AsMouseMoveEvent()) {
                        producer.PublishMouseMoveSourceEvidence(
                            contextSnapshot,
                            move->mouseInputX,
                            move->mouseInputY,
                            tick);
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

    InputFramePump& InputFramePump::GetSingleton()
    {
        static InputFramePump instance;
        return instance;
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
        logger::info("[DualPad][FramePump] Unregistered from BSInputDeviceManager input pump");
    }

    RE::BSEventNotifyControl InputFramePump::ProcessEvent(
        RE::InputEvent* const* event,
        RE::BSTEventSource<RE::InputEvent*>* source)
    {
        (void)source;

        PublishKeyboardMouseEvidence(event);

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

                static bool loggedRouteOwnership = false;
                if (!loggedRouteOwnership) {
                    logger::info(
                        "[DualPad][FramePump] Official upstream gamepad route owns snapshot draining; input pump will assist only when poll activity goes stale");
                    loggedRouteOwnership = true;
                }

                if (upstreamHook.HasRecentPollCallActivity(kUpstreamPollAssistWindowMs)) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto lastPollAgeMs = upstreamHook.GetLastPollCallAgeMs();
                const DrainTelemetryContext telemetry{
                    .reason = DrainReason::FramePumpAssistStale,
                    .routeState = ResolveUpstreamRouteState(
                        upstreamHook.IsRouteActive(),
                        lastPollAgeMs,
                        kUpstreamPollAssistWindowMs),
                    .lastPollAgeMs = lastPollAgeMs,
                    .hookInstalled = upstreamHook.IsInstalled()
                };
                const auto drained =
                    PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread(kUpstreamAssistDrainBudget, &telemetry);
                if (drained != 0) {
                    static std::uint64_t lastAssistLogTickMs = 0;
                    const auto now = GetTickCount64();
                    if (now - lastAssistLogTickMs >= 1000) {
                        logger::warn(
                            "[DualPad][FramePump] Upstream poll activity stale; input pump assisted snapshot drain drained={} budget={} windowMs={} routeState={} lastPollAgeMs={}",
                            drained,
                            kUpstreamAssistDrainBudget,
                            kUpstreamPollAssistWindowMs,
                            ToString(telemetry.routeState),
                            telemetry.lastPollAgeMs ? std::to_string(*telemetry.lastPollAgeMs) : "none");
                        lastAssistLogTickMs = now;
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        const auto lastPollAgeMs = upstreamHook.GetLastPollCallAgeMs();
        const DrainTelemetryContext telemetry{
            .reason = DrainReason::FramePumpDisabled,
            .routeState = ResolveUpstreamRouteState(
                upstreamHook.IsRouteActive(),
                lastPollAgeMs,
                kUpstreamPollAssistWindowMs),
            .lastPollAgeMs = lastPollAgeMs,
            .hookInstalled = upstreamHook.IsInstalled()
        };
        PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread(
            PadEventSnapshotDispatcher::DefaultDrainBudget(),
            &telemetry);

        return RE::BSEventNotifyControl::kContinue;
    }
}

