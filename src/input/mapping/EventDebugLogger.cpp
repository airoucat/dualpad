#include "pch.h"
#include "input/mapping/EventDebugLogger.h"

#include <cstdlib>

namespace logger = SKSE::log;

namespace dualpad::input
{
#if !defined(NDEBUG)
    namespace
    {
        bool EnvEnabled(const char* name)
        {
            char* value = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&value, &length, name) != 0 || !value || value[0] == '\0') {
                std::free(value);
                return false;
            }

            const bool enabled = value[0] != '0';
            std::free(value);
            return enabled;
        }
    }

    bool IsMappingDebugLogEnabled()
    {
        static const bool enabled = EnvEnabled("DUALPAD_LOG_MAPPING_EVENTS");
        return enabled;
    }

    void LogPadEvent(const PadEvent& event)
    {
        if (!IsMappingDebugLogEnabled()) {
            return;
        }

        logger::debug(
            "[DualPad][Mapping] type={} trigger={} code=0x{:08X} modifiers=0x{:08X} axis={} prev={:.3f} value={:.3f} touch={}({},{}) mode={} region={} slide={}",
            ToString(event.type),
            static_cast<int>(event.triggerType),
            event.code,
            event.modifierMask,
            ToString(event.axis),
            event.previousValue,
            event.value,
            event.touchId,
            event.touchX,
            event.touchY,
            ToString(event.touchpadMode),
            ToString(event.touchRegion),
            ToString(event.slideDirection));
    }

    void LogPadEvents(const PadEventBuffer& events)
    {
        if (!IsMappingDebugLogEnabled() || events.count == 0) {
            return;
        }

        for (std::size_t i = 0; i < events.count; ++i) {
            LogPadEvent(events[i]);
        }
    }
#else
    bool IsMappingDebugLogEnabled()
    {
        return false;
    }

    void LogPadEvent(const PadEvent& event)
    {
        (void)event;
    }

    void LogPadEvents(const PadEventBuffer& events)
    {
        (void)events;
    }
#endif
}
