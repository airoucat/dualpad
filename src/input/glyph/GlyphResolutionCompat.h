#pragma once



#include "input_v2/compat/LegacyInputContextCompat.h"

#include "input/Trigger.h"



#include <cstddef>

#include <cstdint>

#include <string>

#include <string_view>



namespace dualpad::input::glyph

{

    enum class GlyphResolutionStatus

    {

        Resolved = 0,

        NoBinding,

        UnsupportedTriggerForToken

    };



    enum class GlyphFallbackKind

    {

        None = 0,

        ContextParseFallbackToMenu,

        ContextRetryToMenu

    };



    enum class GlyphFallbackReason

    {

        None = 0,

        ContextParseFailure,

        NoBinding,

        UnsupportedTriggerForToken

    };



    struct GlyphResolutionCompatResult

    {

        bool ok{ false };

        std::string token;

        std::string semanticId;

        std::string requestedContextName;

        std::string resolvedContextName;

        GlyphResolutionStatus status{ GlyphResolutionStatus::NoBinding };

        GlyphFallbackKind fallbackKind{ GlyphFallbackKind::None };

        GlyphFallbackReason fallbackReason{ GlyphFallbackReason::None };

        TriggerType resolvedTriggerType{ TriggerType::Button };

        std::uint32_t resolvedTriggerCode{ 0 };

        std::size_t candidateCount{ 0 };

        bool reverseLookupAmbiguous{ false };

    };



    GlyphResolutionCompatResult ResolveActionGlyphCompat(

        std::string_view actionId,

        std::string_view contextName);



    const char* ToString(GlyphResolutionStatus status);

    const char* ToString(GlyphFallbackKind fallbackKind);

    const char* ToString(GlyphFallbackReason fallbackReason);

}

