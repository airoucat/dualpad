#pragma once


#include "input/glyph/GlyphResolutionCompat.h"

#include <RE/F/FxDelegateHandler.h>

#include <string>
#include <string_view>

namespace dualpad::input::glyph
{
    class ScaleformGlyphBridge : public RE::FxDelegateHandler
    {
    public:
        static ScaleformGlyphBridge& GetSingleton();

        void RegisterInitialMenus();
        void OnMenuOpened(std::string_view menuName);
        void OnMenuClosed(std::string_view menuName);
        GlyphResolutionCompatResult ReplayResolveActionGlyph(
            std::string_view actionId,
            std::string_view contextName);

        void Accept(CallbackProcessor* processor) override;

    private:
        ScaleformGlyphBridge() = default;
    };
}
