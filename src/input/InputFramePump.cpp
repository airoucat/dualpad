#include "pch.h"
#include "input/InputFramePump.h"

#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/UpstreamGamepadHook.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
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
                static bool logged = false;
                if (!logged) {
                    logger::info(
                        "[DualPad][FramePump] Official upstream gamepad route owns snapshot draining; event sink is observing only");
                    logged = true;
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread();

        return RE::BSEventNotifyControl::kContinue;
    }
}

