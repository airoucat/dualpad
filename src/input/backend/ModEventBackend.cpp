#include "pch.h"
#include "input/backend/ModEventBackend.h"

#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    void ModEventBackend::Dispatch(const FrameActionPlan& plan)
    {
        for (const auto& action : plan) {
            if (action.backend != PlannedBackend::ModEvent) {
                continue;
            }

            logger::trace(
                "[DualPad][ModBackend] Stub dispatch action='{}' phase={} source=0x{:08X}",
                action.actionId,
                static_cast<std::uint32_t>(action.phase),
                action.sourceCode);
        }
    }
}
