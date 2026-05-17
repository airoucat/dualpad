#include "pch.h"



#include "input/BindingManager.h"

#include "input/glyph/GlyphResolutionCompat.h"



#include <stdexcept>

#include <string_view>



namespace

{

    using dualpad::input::Binding;

    using dualpad::input::BindingManager;

    using dualpad::input::InputContext;

    using dualpad::input::Trigger;

    using dualpad::input::TriggerType;

    using dualpad::input::glyph::GlyphFallbackKind;

    using dualpad::input::glyph::GlyphFallbackReason;

    using dualpad::input::glyph::GlyphResolutionStatus;



    void Require(bool condition, std::string_view message)

    {

        if (!condition) {

            throw std::runtime_error(std::string(message));

        }

    }



    Binding MakeBinding(InputContext context, TriggerType type, std::uint32_t code, std::string_view actionId)

    {

        Binding binding{};

        binding.context = context;

        binding.actionId = std::string(actionId);

        binding.trigger.type = type;

        binding.trigger.code = code;

        return binding;

    }



    void ResetBindings()

    {

        BindingManager::GetSingleton().ClearBindings();

    }



    void TestContextParseFallbackToMenu()

    {

        ResetBindings();

        BindingManager::GetSingleton().AddBinding(MakeBinding(InputContext::Menu, TriggerType::Button, 0x00000002, "Menu.Confirm"));



        const auto result = dualpad::input::glyph::ResolveActionGlyphCompat("Menu.Confirm", "UnknownWidget");

        Require(result.ok, "context parse fallback should still resolve via menu");

        Require(result.token == "360_A", "menu fallback should reuse current button token mapping");

        Require(result.status == GlyphResolutionStatus::Resolved, "fallback success should still report resolved");

        Require(result.fallbackKind == GlyphFallbackKind::ContextParseFallbackToMenu, "invalid context should be recorded as parse fallback");

        Require(result.fallbackReason == GlyphFallbackReason::ContextParseFailure, "invalid context should keep parse failure reason");

        Require(result.resolvedContextName == "Menu", "resolved context should be canonical menu");

    }



    void TestSpecificContextMissRetriesMenu()

    {

        ResetBindings();

        BindingManager::GetSingleton().AddBinding(MakeBinding(InputContext::Menu, TriggerType::Button, 0x00000004, "Menu.Cancel"));



        const auto result = dualpad::input::glyph::ResolveActionGlyphCompat("Menu.Cancel", "JournalMenu");

        Require(result.ok, "menu retry should resolve when specific context misses");

        Require(result.token == "360_B", "menu retry should return menu token");

        Require(result.fallbackKind == GlyphFallbackKind::ContextRetryToMenu, "specific context miss should be recorded as menu retry");

        Require(result.fallbackReason == GlyphFallbackReason::NoBinding, "specific context miss should keep no-binding reason");

        Require(result.resolvedContextName == "Menu", "retry should report menu as resolved context");

    }



    void TestUnsupportedTriggerIsExplicit()

    {

        ResetBindings();

        BindingManager::GetSingleton().AddBinding(MakeBinding(InputContext::Menu, TriggerType::Combo, 0x00000002, "Menu.Combo"));



        const auto result = dualpad::input::glyph::ResolveActionGlyphCompat("Menu.Combo", "Menu");

        Require(!result.ok, "unsupported trigger should not produce a token");

        Require(result.status == GlyphResolutionStatus::UnsupportedTriggerForToken, "unsupported trigger should be explicit");

        Require(result.fallbackKind == GlyphFallbackKind::None, "no retry should be recorded when request already targets menu");

        Require(result.resolvedTriggerType == TriggerType::Combo, "resolved trigger type should preserve unsupported trigger kind");

    }



    void TestReverseLookupAmbiguityIsExposed()

    {

        ResetBindings();

        BindingManager::GetSingleton().AddBinding(MakeBinding(InputContext::Menu, TriggerType::Button, 0x00000002, "Menu.Multi"));

        BindingManager::GetSingleton().AddBinding(MakeBinding(InputContext::Menu, TriggerType::Button, 0x00000004, "Menu.Multi"));



        const auto result = dualpad::input::glyph::ResolveActionGlyphCompat("Menu.Multi", "Menu");

        Require(result.ok, "current compat path should still return one token for multi-binding actions");

        Require(result.candidateCount == 2, "candidate count should expose multi-binding ambiguity");

        Require(result.reverseLookupAmbiguous, "multi-binding action should set ambiguity flag");

        Require(result.token == "360_A" || result.token == "360_B", "compat token should remain one of the current first-hit results");

    }

}



int main()

{

    TestContextParseFallbackToMenu();

    TestSpecificContextMissRetriesMenu();

    TestUnsupportedTriggerIsExplicit();

    TestReverseLookupAmbiguityIsExposed();

    return 0;

}

