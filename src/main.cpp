#include "pch.h"
#include "input/HidReader.h"
#include "input/ActionRuntime.h"
#include "input/GameInputHook.h"

namespace logger = SKSE::log;

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)
    {
        if (!msg) return;

        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            logger::info("[DualPad] SKSE message: DataLoaded -> InitActionRuntime");
            dualpad::input::InitActionRuntime();
            dualpad::input::InstallGameInputHook();
            dualpad::input::StartActionRuntimeTickOnMainThread();
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    logger::info("DualPad loaded");

    if (auto* messaging = SKSE::GetMessagingInterface(); messaging) {
        if (!messaging->RegisterListener(OnSKSEMessage)) {
            logger::warn("Failed to register SKSE messaging listener");
        }
    }

    dualpad::input::StartHidReader();
    return true;
}