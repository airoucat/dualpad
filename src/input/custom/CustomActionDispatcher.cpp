#include "pch.h"
#include "input/custom/CustomActionDispatcher.h"
#include "input/Action.h"
#include "input/custom/ScreenshotAction.h"
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input::custom
{
    namespace
    {
        void LogScreenshotResult(const ScreenshotRequestResult& result)
        {
            switch (result.status) {
            case ScreenshotRequestStatus::Queued:
                logger::info("[DualPad][CustomAction] Screenshot queued: {} (queue depth: {})",
                    result.filename,
                    result.queueDepth);
                break;
            case ScreenshotRequestStatus::Debounced:
                logger::trace("[DualPad][CustomAction] Screenshot request debounced");
                break;
            case ScreenshotRequestStatus::NotReady:
                logger::warn("[DualPad][CustomAction] Screenshot service is not ready");
                break;
            case ScreenshotRequestStatus::UnsupportedFormat:
                logger::error("[DualPad][CustomAction] Screenshot failed: unsupported back buffer format");
                break;
            case ScreenshotRequestStatus::CaptureFailed:
                logger::error("[DualPad][CustomAction] Screenshot capture failed");
                break;
            }
        }
    }

    CustomActionDispatcher& CustomActionDispatcher::GetSingleton()
    {
        static CustomActionDispatcher instance;
        return instance;
    }

    void CustomActionDispatcher::Start()
    {
        ScreenshotActionService::GetSingleton().Start();
    }

    void CustomActionDispatcher::Stop()
    {
        ScreenshotActionService::GetSingleton().Stop();
    }

    bool CustomActionDispatcher::Execute(std::string_view actionId)
    {
        if (actionId == actions::Screenshot) {
            return ExecuteScreenshotAction();
        }

        return false;
    }

    bool CustomActionDispatcher::ExecuteScreenshotAction()
    {
        logger::info("[DualPad][CustomAction] Scheduling screenshot request");

        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            logger::warn("[DualPad][CustomAction] Failed to get TaskInterface for screenshot request");
            return false;
        }

        taskInterface->AddTask([]() {
            const auto result = ScreenshotActionService::GetSingleton().RequestCapture();
            LogScreenshotResult(result);
            });

        return true;
    }
}
