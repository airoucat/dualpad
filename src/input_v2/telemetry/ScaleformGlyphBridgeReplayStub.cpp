#include "pch.h"
#include "input/glyph/ScaleformGlyphBridge.h"

#include "input/RuntimeConfig.h"
#include "input_v2/telemetry/InputTraceRecorder.h"

namespace dualpad::input::glyph
{
    ScaleformGlyphBridge& ScaleformGlyphBridge::GetSingleton()
    {
        static ScaleformGlyphBridge instance;
        return instance;
    }

    void ScaleformGlyphBridge::RegisterInitialMenus()
    {
    }

    void ScaleformGlyphBridge::OnMenuOpened(std::string_view)
    {
    }

    GlyphResolutionCompatResult ScaleformGlyphBridge::ReplayResolveActionGlyph(
        std::string_view actionId,
        std::string_view contextName)
    {
        const auto resolution = ResolveActionGlyphCompat(actionId, contextName);
        if (RuntimeConfig::GetSingleton().TraceRecordGlyphQueries()) {
            input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordGlyphResult(
                actionId,
                contextName,
                resolution);
        }
        return resolution;
    }

    void ScaleformGlyphBridge::Accept(CallbackProcessor*)
    {
    }
}
