#include "pch.h"
#include "input/HidReader.h"
#include "input/IATHook.h"

namespace logger = SKSE::log;

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)
    {
        if (!msg) {
            return;
        }

        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            logger::info("[DualPad] Initializing systems");

            // 安装 IAT hook
            const bool usesXInput = dualpad::input::InstallXInputIATHook();

            if (usesXInput) {
                logger::info("[DualPad] XInput IAT hook active");
            }
            else {
                logger::warn("[DualPad] Skyrim does not use XInput");
            }

            // 启动 HID 读取线程
            dualpad::input::StartHidReader();

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