#pragma once

#include "input_v2/prompt/PromptRuntimeOwner.h"

#include <RE/F/FxDelegateHandler.h>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace dualpad::input::glyph
{
    struct GlyphResolutionCompatResult;
}

namespace dualpad::input_v2::prompt
{
    class ScaleformPromptAdapter : public RE::FxDelegateHandler
    {
    public:
        static ScaleformPromptAdapter& GetSingleton();

        void RegisterInitialMenus();
        void OnMenuOpened(std::string_view menuName);
        void OnMenuClosed(std::string_view menuName);

        [[nodiscard]] std::string ResolveLegacyGlyphTokenForRuntime(
            std::string_view actionId,
            std::string_view contextName);
        [[nodiscard]] PromptLegacyGlyphDescriptor ResolveLegacyGlyphForRuntime(
            std::string_view actionId,
            std::string_view contextName);
        [[nodiscard]] input::glyph::GlyphResolutionCompatResult ResolveCompatForReplay(
            std::string_view actionId,
            std::string_view contextName);

        void Accept(CallbackProcessor* processor) override;

    private:
        ScaleformPromptAdapter() = default;

        static void HandleGetActionGlyphToken(const RE::FxDelegateArgs& args);
        static void HandleGetActionGlyph(const RE::FxDelegateArgs& args);

        bool AttachToMenu(std::string_view menuName);

        std::mutex _mutex;
        std::unordered_map<std::string, std::uintptr_t> _registeredDelegatesByMenu;
    };
}
