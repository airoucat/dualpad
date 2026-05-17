#include "pch.h"

#include "input/glyph/GlyphResolutionCompat.h"



#include "input/BindingManager.h"

#include "input/InputContextNames.h"

#include "input/mapping/PadEvent.h"



#include <optional>



namespace dualpad::input::glyph

{

    namespace

    {

        constexpr std::uint32_t kSquare = 0x00000001;

        constexpr std::uint32_t kCross = 0x00000002;

        constexpr std::uint32_t kCircle = 0x00000004;

        constexpr std::uint32_t kTriangle = 0x00000008;

        constexpr std::uint32_t kL1 = 0x00000010;

        constexpr std::uint32_t kR1 = 0x00000020;

        constexpr std::uint32_t kL2Button = 0x00000040;

        constexpr std::uint32_t kR2Button = 0x00000080;

        constexpr std::uint32_t kCreate = 0x00000100;

        constexpr std::uint32_t kOptions = 0x00000200;

        constexpr std::uint32_t kDpadUp = 0x00010000;

        constexpr std::uint32_t kDpadDown = 0x00020000;

        constexpr std::uint32_t kDpadLeft = 0x00040000;

        constexpr std::uint32_t kDpadRight = 0x00080000;



        std::optional<std::string> ButtonCodeToToken(std::uint32_t code)

        {

            switch (code) {

            case kSquare:

                return "360_X";

            case kCross:

                return "360_A";

            case kCircle:

                return "360_B";

            case kTriangle:

                return "360_Y";

            case kL1:

                return "360_LB";

            case kR1:

                return "360_RB";

            case kL2Button:

                return "360_LT";

            case kR2Button:

                return "360_RT";

            case kCreate:

                return "360_Back";

            case kOptions:

                return "360_Start";

            case kDpadUp:

                return "360_DPAD_UP";

            case kDpadDown:

                return "360_DPAD_DOWN";

            case kDpadLeft:

                return "360_DPAD_LEFT";

            case kDpadRight:

                return "360_DPAD_RIGHT";

            default:

                return std::nullopt;

            }

        }



        std::optional<std::string> TriggerToButtonArtToken(const Trigger& trigger)

        {

            switch (trigger.type) {

            case TriggerType::Button:

            case TriggerType::Tap:

            case TriggerType::Hold:

                return ButtonCodeToToken(trigger.code);

            case TriggerType::Axis:

                if (trigger.code == static_cast<std::uint32_t>(PadAxisId::LeftTrigger)) {

                    return std::string("360_LT");

                }

                if (trigger.code == static_cast<std::uint32_t>(PadAxisId::RightTrigger)) {

                    return std::string("360_RT");

                }

                return std::nullopt;

            default:

                return std::nullopt;

            }

        }



        bool TryResolveActionGlyphForContext(

            std::string_view actionId,

            InputContext context,

            GlyphResolutionCompatResult& result)

        {

            auto& bindingManager = BindingManager::GetSingleton();

            result.resolvedContextName = std::string(ToString(context));

            result.candidateCount = bindingManager.CountTriggersForAction(actionId, context);

            result.reverseLookupAmbiguous = result.candidateCount > 1;



            const auto trigger = bindingManager.GetTriggerForAction(actionId, context);

            if (!trigger) {

                result.status = GlyphResolutionStatus::NoBinding;

                result.resolvedTriggerType = TriggerType::Button;

                result.resolvedTriggerCode = 0;

                result.token.clear();

                result.ok = false;

                return false;

            }



            result.resolvedTriggerType = trigger->type;

            result.resolvedTriggerCode = trigger->code;

            const auto token = TriggerToButtonArtToken(*trigger);

            if (!token) {

                result.status = GlyphResolutionStatus::UnsupportedTriggerForToken;

                result.token.clear();

                result.ok = false;

                return false;

            }



            result.status = GlyphResolutionStatus::Resolved;

            result.token = *token;

            result.ok = true;

            return true;

        }

    }



    GlyphResolutionCompatResult ResolveActionGlyphCompat(

        std::string_view actionId,

        std::string_view contextName)

    {

        GlyphResolutionCompatResult result{};

        result.semanticId = std::string(actionId);

        result.requestedContextName = std::string(contextName);

        result.resolvedContextName = std::string(contextName);



        const auto parsedContext = ParseInputContextName(contextName);

        auto requestedContext = parsedContext.value_or(InputContext::Menu);

        if (!parsedContext) {

            result.fallbackKind = GlyphFallbackKind::ContextParseFallbackToMenu;

            result.fallbackReason = GlyphFallbackReason::ContextParseFailure;

        }



        if (TryResolveActionGlyphForContext(actionId, requestedContext, result)) {

            return result;

        }



        if (parsedContext && requestedContext != InputContext::Menu) {

            const auto initialStatus = result.status;

            result.fallbackKind = GlyphFallbackKind::ContextRetryToMenu;

            result.fallbackReason =

                initialStatus == GlyphResolutionStatus::UnsupportedTriggerForToken ?

                GlyphFallbackReason::UnsupportedTriggerForToken :

                GlyphFallbackReason::NoBinding;

            (void)TryResolveActionGlyphForContext(actionId, InputContext::Menu, result);

        }



        return result;

    }



    const char* ToString(GlyphResolutionStatus status)

    {

        switch (status) {

        case GlyphResolutionStatus::Resolved:

            return "resolved";

        case GlyphResolutionStatus::NoBinding:

            return "no_binding";

        case GlyphResolutionStatus::UnsupportedTriggerForToken:

        default:

            return "unsupported_trigger_for_token";

        }

    }



    const char* ToString(GlyphFallbackKind fallbackKind)

    {

        switch (fallbackKind) {

        case GlyphFallbackKind::None:

            return "none";

        case GlyphFallbackKind::ContextParseFallbackToMenu:

            return "context_parse_fallback_to_menu";

        case GlyphFallbackKind::ContextRetryToMenu:

        default:

            return "context_retry_to_menu";

        }

    }



    const char* ToString(GlyphFallbackReason fallbackReason)

    {

        switch (fallbackReason) {

        case GlyphFallbackReason::None:

            return "none";

        case GlyphFallbackReason::ContextParseFailure:

            return "context_parse_failure";

        case GlyphFallbackReason::NoBinding:

            return "no_binding";

        case GlyphFallbackReason::UnsupportedTriggerForToken:

        default:

            return "unsupported_trigger_for_token";

        }

    }

}

