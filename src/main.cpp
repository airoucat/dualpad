#include "pch.h"

#include <SKSE/SKSE.h>

#include "input/HidReader.h"

#include "input/IATHook.h"

#include "input/ContextEventSink.h"

#include "input/BindingConfig.h"

#include "input/InputFramePump.h"

#include "input/RuntimeConfig.h"
#include "input/backend/KeyboardNativeBackend.h"

#include "input/injection/GamepadFactoryXInputBypassHook.h"
#include "input/injection/NativeInputPreControlMapHook.h"
#include "input/injection/GamepadDeviceCreationProbeHook.h"
#include "input/injection/OrbisGamepadProbeHook.h"
#include "input/injection/UpstreamGamepadHook.h"

#include "input/custom/CustomActionDispatcher.h"

#include "haptics/HapticsSystem.h"

#include <cstdlib>

#include <atomic>

namespace logger = SKSE::log;

namespace
{
    void LogReverseProbeAddresses()
    {
#ifdef DUALPAD_DIAGNOSTIC_BUILD
        static bool logged = false;
        if (logged) {
            return;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const REL::Relocation<std::uintptr_t> pollInputDevices{ RELOCATION_ID(67315, 68617) };
        logger::info(
            "[DualPad][Diag] ExeBase=0x{:X}, Poll=0x{:X}, RVA=0x{:X}",
            base,
            pollInputDevices.address(),
            pollInputDevices.address() - base);
        logged = true;
#endif
    }

    // SKSE sends DataLoaded once forms and UI systems are ready for plugin startup.

    void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)

    {

        if (!msg) {

            return;

        }

        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {

            logger::info("[DualPad] Initializing systems");
            LogReverseProbeAddresses();

            dualpad::input::ContextEventSink::GetSingleton().Register();

            dualpad::input::RuntimeConfig::GetSingleton().Load();

            dualpad::input::BindingConfig::GetSingleton().Load();

            dualpad::input::InputFramePump::GetSingleton().Register();

            if (dualpad::input::RuntimeConfig::GetSingleton().UseUpstreamGamepadHook()) {
                dualpad::input::UpstreamGamepadHook::GetSingleton().Install();
            }

            if (dualpad::input::RuntimeConfig::GetSingleton().UseOrbisGamepadProbeHook()) {
                dualpad::input::OrbisGamepadProbeHook::GetSingleton().Install();
            }

            if (dualpad::input::RuntimeConfig::GetSingleton().UseGamepadDeviceCreationProbeHook()) {
                dualpad::input::GamepadDeviceCreationProbeHook::GetSingleton().Install();
            }

            if (dualpad::input::RuntimeConfig::GetSingleton().ForceFactoryXInputCapabilitiesFail()) {
                dualpad::input::GamepadFactoryXInputBypassHook::GetSingleton().Install();
            }

            if (dualpad::input::RuntimeConfig::GetSingleton().UseUpstreamKeyboardHook()) {
                dualpad::input::backend::KeyboardNativeBackend::GetSingleton().Install();
            }

            // Do not force mixed platform arbitration during gameplay. Current
            // experiments show global gamepad-family overrides break
            // keyboard-native semantics even when dinput8 bridge injection is
            // otherwise correct.

            if (dualpad::input::RuntimeConfig::GetSingleton().UseNativeButtonInjector()) {
                dualpad::input::NativeInputPreControlMapHook::GetSingleton().Install();
            }

            dualpad::input::custom::CustomActionDispatcher::GetSingleton().Start();

            const bool usesXInput = dualpad::input::InstallXInputIATHook();

            if (usesXInput) {

                logger::info("[DualPad] XInput IAT hook active");

            }

            else {

                logger::warn("[DualPad] Skyrim does not use XInput");

            }

            dualpad::input::StartHidReader();

            auto& hapticsSystem = dualpad::haptics::HapticsSystem::GetSingleton();

            if (hapticsSystem.Initialize()) {

                if (hapticsSystem.Start()) {

                    logger::info("[DualPad] Haptics system started successfully");

                }

                else {

                    logger::error("[DualPad] Failed to start haptics system");

                }

            }

            else {

                logger::warn("[DualPad] Haptics system disabled or failed to initialize");

            }

            if (auto* console = RE::ConsoleLog::GetSingleton(); console) {

                console->Print("DualPad Haptics loaded.");

            }

            logger::info("[DualPad] Initialization complete");

        }

    }

}

SKSEPluginLoad(const SKSE::LoadInterface* skse)

{

    SKSE::Init(skse);
    SKSE::AllocTrampoline(1 << 9);

    logger::info("DualPad v1.0.0 loaded");

    // The gamepad device factory can run before DataLoaded, so creation probes
    // need their config and hook installed as early as plugin load.
    dualpad::input::RuntimeConfig::GetSingleton().Load();
    if (dualpad::input::RuntimeConfig::GetSingleton().UseGamepadDeviceCreationProbeHook()) {
        dualpad::input::GamepadDeviceCreationProbeHook::GetSingleton().Install();
    }
    if (dualpad::input::RuntimeConfig::GetSingleton().ForceFactoryXInputCapabilitiesFail()) {
        dualpad::input::GamepadFactoryXInputBypassHook::GetSingleton().Install();
    }

    if (auto* messaging = SKSE::GetMessagingInterface(); messaging) {

        if (!messaging->RegisterListener(OnSKSEMessage)) {

            logger::warn("[DualPad] Failed to register SKSE messaging listener");

        }

    }

    return true;

}
