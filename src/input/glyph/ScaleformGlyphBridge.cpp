#include "pch.h"

#include "input/glyph/ScaleformGlyphBridge.h"

#include "input/RuntimeConfig.h"
#include "input_v2/prompt/ScaleformPromptAdapter.h"
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
        input_v2::prompt::ScaleformPromptAdapter::GetSingleton().RegisterInitialMenus();
    }

    void ScaleformGlyphBridge::OnMenuOpened(std::string_view menuName)
    {
        input_v2::prompt::ScaleformPromptAdapter::GetSingleton().OnMenuOpened(menuName);
    }

    void ScaleformGlyphBridge::OnMenuClosed(std::string_view menuName)
    {
        input_v2::prompt::ScaleformPromptAdapter::GetSingleton().OnMenuClosed(menuName);
    }

    GlyphResolutionCompatResult ScaleformGlyphBridge::ReplayResolveActionGlyph(
        std::string_view actionId,
        std::string_view contextName)
    {
        const auto resolution = input_v2::prompt::ScaleformPromptAdapter::GetSingleton().ResolveCompatForReplay(
            actionId,
            contextName);
        if (RuntimeConfig::GetSingleton().TraceRecordGlyphQueries()) {
            input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordGlyphResult(
                actionId,
                contextName,
                resolution);
        }
        return resolution;
    }

    void ScaleformGlyphBridge::Accept(CallbackProcessor* processor)
    {
        input_v2::prompt::ScaleformPromptAdapter::GetSingleton().Accept(processor);
    }
}
