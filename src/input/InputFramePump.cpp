#include "pch.h"
#include "input/InputFramePump.h"

#include "input/HidReader.h"
#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/UpstreamGamepadHook.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::size_t kUpstreamAssistDrainBudget = 32;
        constexpr std::uint64_t kUpstreamPollAssistWindowMs = 250;
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
        (void)event;
        (void)source;

        if (RuntimeConfig::GetSingleton().UseUpstreamGamepadHook()) {
            auto& upstreamHook = UpstreamGamepadHook::GetSingleton();
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

                const auto drained =
                    PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread(kUpstreamAssistDrainBudget);
                if (drained != 0) {
                    static std::uint64_t lastAssistLogTickMs = 0;
                    const auto now = GetTickCount64();
                    if (now - lastAssistLogTickMs >= 1000) {
                        logger::warn(
                            "[DualPad][FramePump] Upstream poll activity stale; input pump assisted snapshot drain drained={} budget={} windowMs={}",
                            drained,
                            kUpstreamAssistDrainBudget,
                            kUpstreamPollAssistWindowMs);
                        lastAssistLogTickMs = now;
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread();

        return RE::BSEventNotifyControl::kContinue;
    }
}

