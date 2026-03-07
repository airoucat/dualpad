#include "pch.h"

#include <SKSE/SKSE.h>

#include "input/HidReader.h"

#include "input/IATHook.h"

#include "input/ContextEventSink.h"

#include "input/BindingConfig.h"

#include "input/ScreenshotManager.h"

#include "haptics/HapticsSystem.h"

#include <cstdlib>

#include <atomic>

namespace logger = SKSE::log;

namespace
{
    // SKSE sends DataLoaded once forms and UI systems are ready for plugin startup.

    void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)

    {

        if (!msg) {

            return;

        }

        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {

            logger::info("[DualPad] Initializing systems");

            dualpad::input::ContextEventSink::GetSingleton().Register();

            dualpad::input::BindingConfig::GetSingleton().Load();

            dualpad::utils::ScreenshotManager::GetSingleton().Start();

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

    logger::info("DualPad v1.0.0 loaded");

    if (auto* messaging = SKSE::GetMessagingInterface(); messaging) {

        if (!messaging->RegisterListener(OnSKSEMessage)) {

            logger::warn("[DualPad] Failed to register SKSE messaging listener");

        }

    }

    return true;

}
