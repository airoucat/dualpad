#include "pch.h"
#include "input/HidReader.h"
#include "input/ActionRuntime.h"
#include "input/GameInputHook.h"

namespace logger = SKSE::log;

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)
    {
        if (!msg) {
            return;
        }

        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            logger::info("[DualPad] SKSE message: DataLoaded -> initialize systems");

            dualpad::input::InitActionRuntime();

            // 先装输入 hook
            dualpad::input::InstallGameInputHook();

            // 再装 native submitter（你已合并到 GameInputHook.cpp）
            // dualpad::input::InstallNativeSubmitter();

            // 最后启动读取线程，确保 submitter 已可用
            dualpad::input::StartHidReader();

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

    // 不要在这里启动 HID，等 DataLoaded 再启动
    return true;
}