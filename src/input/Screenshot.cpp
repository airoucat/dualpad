#include "pch.h"
#include "input/Screenshot.h"
#include "input/ScreenshotManager.h"
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::utils
{
    std::string TakeScreenshot()
    {
        logger::info("[Screenshot] Requesting screenshot capture...");

        auto& manager = ScreenshotManager::GetSingleton();

        if (manager.CaptureFrame()) {
            size_t queueSize = manager.GetQueueSize();
            logger::info("[Screenshot] Screenshot queued for saving ({} in queue)", queueSize);

            if (queueSize > 5) {
                logger::warn("[Screenshot] Queue is getting large, screenshots may be delayed");
            }

            return "queued";
        }
        else {
            logger::error("[Screenshot] Failed to capture screenshot");
            return "";
        }
    }
}
