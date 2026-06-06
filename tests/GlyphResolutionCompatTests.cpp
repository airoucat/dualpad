#include "pch.h"

#include "input/glyph/GlyphResolutionCompat.h"
#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/presentation/PresentationProjection.h"
#include "input_v2/prompt/PromptRuntimeOwner.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    namespace actions = dualpad::input_v2::actions;
    namespace config = dualpad::input_v2::config;
    namespace context = dualpad::input_v2::context;
    namespace presentation = dualpad::input_v2::presentation;
    namespace prompt = dualpad::input_v2::prompt;
    namespace glyph = dualpad::input::glyph;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    std::filesystem::path FindProjectRoot(std::filesystem::path from = std::filesystem::current_path())
    {
        while (!from.empty()) {
            if (std::filesystem::is_regular_file(from / "xmake.lua")) {
                return from;
            }
            const auto parent = from.parent_path();
            if (parent == from) {
                break;
            }
            from = parent;
        }
        throw std::runtime_error("could not find project root");
    }

    void ResetRuntimePromptOwner()
    {
        prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
        actions::CompiledActionGraphPublisher::GetRuntimeOwner().ResetForTests();
        config::AtomicConfigReloader::GetSingleton().ResetForTests();
    }

    void LoadRuntimeConfig()
    {
        const auto root = FindProjectRoot();
        const auto loaded = config::AtomicConfigReloader::GetSingleton().LoadOrRecover(
            root / "config" / "DualPadBindings.ini",
            root / "config" / "DualPadMenuPolicy.ini");
        Require(loaded.ok, loaded.message);
    }

    presentation::PublishedPresentationState MenuPresentation()
    {
        presentation::PublishedPresentationState state{};
        state.family = presentation::DeviceFamily::Gamepad;
        state.uiContextId = context::UiContextId::UnknownTrackedMenu;
        state.actionSetStack = actions::ActionSetStack{
            .baseSetId = "MenuBase",
            .layerIds = { "UnknownTrackedMenuLayer" },
            .scopeAnchorIds = { "MenuBase", "UnknownTrackedMenuLayer" }
        };
        state.epoch = 1;
        return state;
    }

    void PublishMenuPromptScope()
    {
        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        const auto graphSnapshot = actions::CompiledActionGraphPublisher::GetRuntimeOwner().GetActiveSnapshot();
        prompt::PromptRuntimeOwner::GetSingleton().PublishPresentationState(
            MenuPresentation(),
            prompt::PromptRuntimeBaseline{
                .manifestEpoch = graphSnapshot.manifestEpoch,
                .configGeneration = bundle ? bundle->manifestEpoch : 0,
                .bundle = bundle,
                .graph = graphSnapshot
            });
    }

    void TestMissingScopeFailsClosed()
    {
        ResetRuntimePromptOwner();
        LoadRuntimeConfig();

        const auto result = glyph::ResolveActionGlyphCompat("Menu.Confirm", "Menu");
        Require(!result.ok, "compat wrapper must fail closed when PromptRuntimeOwner has no prompt scope");
        Require(result.token.empty(), "missing scope must not invent a glyph token");
        Require(result.status == glyph::GlyphResolutionStatus::NoBinding, "missing scope maps to legacy no-binding status");
        Require(result.fallbackKind == glyph::GlyphFallbackKind::None, "missing scope must not report legacy fallback");
    }

    void TestSuccessUsesPromptRuntimeOwnerDisplayBindingToken()
    {
        ResetRuntimePromptOwner();
        LoadRuntimeConfig();
        PublishMenuPromptScope();

        auto& owner = prompt::PromptRuntimeOwner::GetSingleton();
        const auto ownerToken = owner.ResolveLegacyGlyphToken("Menu.Confirm", "Menu");
        Require(ownerToken == "360_Y", "runtime owner must resolve compiled display binding token for Menu.Confirm");

        const auto result = glyph::ResolveActionGlyphCompat("Menu.Confirm", "Menu");
        Require(result.ok, "compat wrapper must resolve through PromptRuntimeOwner");
        Require(result.token == ownerToken, "compat wrapper token must match PromptRuntimeOwner token");
        Require(result.token == "360_Y", "compat success must come from compiled display binding ButtonArt token");
        Require(result.status == glyph::GlyphResolutionStatus::Resolved, "compat success must report resolved");
        Require(result.fallbackKind == glyph::GlyphFallbackKind::None, "compat success must not use legacy Menu fallback");
        Require(result.candidateCount == 1, "compiled prompt result should expose one visible candidate for Menu.Confirm");
        Require(!result.reverseLookupAmbiguous, "single compiled prompt binding must not report reverse lookup ambiguity");
    }

    void TestInvalidContextFailsClosedWithoutMenuFallback()
    {
        ResetRuntimePromptOwner();
        LoadRuntimeConfig();
        PublishMenuPromptScope();

        const auto ownerToken = prompt::PromptRuntimeOwner::GetSingleton().ResolveLegacyGlyphToken(
            "Menu.Confirm",
            "NotAContext");
        Require(ownerToken.empty(), "PromptRuntimeOwner must fail closed for invalid context");

        const auto result = glyph::ResolveActionGlyphCompat("Menu.Confirm", "NotAContext");
        Require(!result.ok, "compat wrapper invalid context must fail closed");
        Require(result.token.empty(), "invalid context must not fall back to Menu token");
        Require(result.status == glyph::GlyphResolutionStatus::NoBinding, "invalid context maps to legacy no-binding status");
        Require(result.fallbackKind == glyph::GlyphFallbackKind::None, "invalid context must not report parse fallback");
        Require(result.fallbackReason == glyph::GlyphFallbackReason::None, "invalid context must not report retry reason");
        Require(result.resolvedContextName == "NotAContext", "compat wrapper must not rewrite invalid context to Menu");
    }
}

int main()
{
    try {
        TestMissingScopeFailsClosed();
        TestSuccessUsesPromptRuntimeOwnerDisplayBindingToken();
        TestInvalidContextFailsClosedWithoutMenuFallback();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
