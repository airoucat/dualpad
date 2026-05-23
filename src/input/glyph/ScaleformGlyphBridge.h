#pragma once


#include "input/glyph/GlyphResolutionCompat.h"

#include <RE/F/FxDelegateHandler.h>

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace dualpad::input::glyph
{
    class ScaleformGlyphBridge : public RE::FxDelegateHandler
    {
    public:
        static ScaleformGlyphBridge& GetSingleton();

        void RegisterInitialMenus();
        void OnMenuOpened(std::string_view menuName);
        GlyphResolutionCompatResult ReplayResolveActionGlyph(
            std::string_view actionId,
            std::string_view contextName);

        void Accept(CallbackProcessor* processor) override;

    private:
        ScaleformGlyphBridge() = default;

        static void HandleGetActionGlyphToken(const RE::FxDelegateArgs& args);
        static void HandleGetActionGlyph(const RE::FxDelegateArgs& args);

        bool AttachToMenu(std::string_view menuName);

        std::mutex _mutex;
        std::unordered_set<std::uintptr_t> _registeredDelegates;
    };
}
