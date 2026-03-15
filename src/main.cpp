#include "pch.h"

#include <SKSE/SKSE.h>

#include "input/HidReader.h"

#include "input/XInputHapticsBridge.h"

#include "input/ContextEventSink.h"

#include "input/BindingConfig.h"

#include "input/InputFramePump.h"

#include "input/RuntimeConfig.h"
#include "input/backend/KeyboardNativeBackend.h"

#include "input/injection/NativeInputPreControlMapHook.h"
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

            const bool usesXInputSetState = dualpad::input::InstallXInputHapticsBridge();

            if (usesXInputSetState) {

                logger::info("[DualPad] XInputSetState IAT hook active for haptics");

            }

            else {

                logger::warn("[DualPad] Skyrim does not expose XInputSetState; haptics bridge disabled");

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

    if (auto* messaging = SKSE::GetMessagingInterface(); messaging) {

        if (!messaging->RegisterListener(OnSKSEMessage)) {

            logger::warn("[DualPad] Failed to register SKSE messaging listener");

        }

    }

    return true;

}
