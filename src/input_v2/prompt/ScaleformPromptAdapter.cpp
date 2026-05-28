#include "pch.h"

#include "input_v2/prompt/ScaleformPromptAdapter.h"

#include "input/glyph/GlyphResolutionCompat.h"

#include <RE/F/FxDelegateArgs.h>
#include <RE/F/FxResponseArgs.h>
#include <RE/M/MainMenu.h>
#include <RE/U/UI.h>

namespace logger = SKSE::log;

namespace dualpad::input_v2::prompt
{
    namespace
    {
        input::glyph::GlyphResolutionStatus ToGlyphStatus(PromptQueryStatus status)
        {
            using input::glyph::GlyphResolutionStatus;
            switch (status) {
            case PromptQueryStatus::Ok:
                return GlyphResolutionStatus::Resolved;
            case PromptQueryStatus::HiddenOnly:
            case PromptQueryStatus::DeviceFamilyMismatch:
                return GlyphResolutionStatus::UnsupportedTriggerForToken;
            case PromptQueryStatus::ScopeUnavailable:
            case PromptQueryStatus::UnknownAction:
            case PromptQueryStatus::UnknownContext:
            case PromptQueryStatus::ContextOutOfScope:
            case PromptQueryStatus::NoVisibleBinding:
            default:
                return GlyphResolutionStatus::NoBinding;
            }
        }

        input::glyph::GlyphResolutionCompatResult MakeReplayCompatResult(
            std::string_view actionId,
            std::string_view contextName,
            const PromptDescriptor& descriptor)
        {
            input::glyph::GlyphResolutionCompatResult result{};
            result.ok = descriptor.ok;
            result.semanticId = std::string(actionId);
            result.requestedContextName = std::string(contextName);
            result.resolvedContextName = std::string(contextName);
            result.status = ToGlyphStatus(descriptor.status);
            result.fallbackKind = input::glyph::GlyphFallbackKind::None;
            result.fallbackReason = input::glyph::GlyphFallbackReason::None;
            if (descriptor.primary) {
                result.token = descriptor.primary->token;
                result.candidateCount = descriptor.alternates.size() + 1;
            }
            result.reverseLookupAmbiguous = !descriptor.alternates.empty();
            return result;
        }
    }

    ScaleformPromptAdapter& ScaleformPromptAdapter::GetSingleton()
    {
        static ScaleformPromptAdapter adapter;
        return adapter;
    }

    void ScaleformPromptAdapter::RegisterInitialMenus()
    {
        if (auto* task = SKSE::GetTaskInterface(); task) {
            task->AddUITask([] {
                (void)ScaleformPromptAdapter::GetSingleton().AttachToMenu(RE::MainMenu::MENU_NAME);
            });
        }
    }

    void ScaleformPromptAdapter::OnMenuOpened(std::string_view menuName)
    {
        const auto menu = std::string(menuName);
        AttachToMenu(menu);

        if (auto* task = SKSE::GetTaskInterface(); task) {
            task->AddUITask([menu] {
                (void)ScaleformPromptAdapter::GetSingleton().AttachToMenu(menu);
            });
        }
    }

    std::string ScaleformPromptAdapter::ResolveLegacyGlyphTokenForRuntime(
        std::string_view actionId,
        std::string_view contextName)
    {
        return PromptRuntimeOwner::GetSingleton().ResolveLegacyGlyphToken(actionId, contextName);
    }

    PromptLegacyGlyphDescriptor ScaleformPromptAdapter::ResolveLegacyGlyphForRuntime(
        std::string_view actionId,
        std::string_view contextName)
    {
        return PromptRuntimeOwner::GetSingleton().ResolveLegacyGlyph(actionId, contextName);
    }

    input::glyph::GlyphResolutionCompatResult ScaleformPromptAdapter::ResolveCompatForReplay(
        std::string_view actionId,
        std::string_view contextName)
    {
        const PromptQuery query{
            .actionId = actionId,
            .selectorKind = PromptScopeSelectorKind::ExplicitContextName,
            .contextName = contextName
        };
        return MakeReplayCompatResult(
            actionId,
            contextName,
            PromptRuntimeOwner::GetSingleton().Resolve(query));
    }

    void ScaleformPromptAdapter::Accept(CallbackProcessor* processor)
    {
        if (!processor) {
            return;
        }

        processor->Process("DualPad_GetActionGlyphToken", HandleGetActionGlyphToken);
        processor->Process("DualPad_GetActionGlyph", HandleGetActionGlyph);
    }

    void ScaleformPromptAdapter::HandleGetActionGlyphToken(const RE::FxDelegateArgs& args)
    {
        RE::FxResponseArgs<1> response;
        response.Add(RE::GFxValue(""));

        if (args.GetArgCount() < 2 || !args[0].IsString() || !args[1].IsString()) {
            args.Respond(response);
            return;
        }

        const char* actionId = args[0].GetString();
        const char* contextName = args[1].GetString();
        if (!actionId || !contextName) {
            args.Respond(response);
            return;
        }

        const auto token = PromptRuntimeOwner::GetSingleton().ResolveLegacyGlyphToken(actionId, contextName);
        if (!token.empty()) {
            response = RE::FxResponseArgs<1>();
            response.Add(RE::GFxValue(token.c_str()));
        }

        logger::info(
            "[DualPad][PromptAdapter] GameDelegate token action={} requestedContext={} token={}",
            actionId,
            contextName,
            token.empty() ? std::string("<none>") : token);

        args.Respond(response);
    }

    void ScaleformPromptAdapter::HandleGetActionGlyph(const RE::FxDelegateArgs& args)
    {
        RE::GFxValue emptyResult;
        emptyResult.SetNull();

        if (args.GetArgCount() < 2 || !args[0].IsString() || !args[1].IsString()) {
            args.Respond(emptyResult);
            return;
        }

        const char* actionId = args[0].GetString();
        const char* contextName = args[1].GetString();
        if (!actionId || !contextName) {
            args.Respond(emptyResult);
            return;
        }

        auto* movie = args.GetMovie();
        if (!movie) {
            args.Respond(emptyResult);
            return;
        }

        const auto descriptor = PromptRuntimeOwner::GetSingleton().ResolveLegacyGlyph(actionId, contextName);

        RE::GFxValue result;
        movie->CreateObject(&result);
        result.SetMember("ok", RE::GFxValue(descriptor.ok));
        result.SetMember("buttonArtToken", RE::GFxValue(descriptor.buttonArtToken.c_str()));
        result.SetMember("semanticId", RE::GFxValue(descriptor.semanticId.c_str()));
        result.SetMember("contextName", RE::GFxValue(descriptor.contextName.c_str()));
        result.SetMember("failureReason", RE::GFxValue(descriptor.failureReason.c_str()));
        result.SetMember("resolvedContextId", RE::GFxValue(descriptor.resolvedContextId.c_str()));
        result.SetMember("resolvedActionSetId", RE::GFxValue(descriptor.resolvedActionSetId.c_str()));
        result.SetMember("resolutionSource", RE::GFxValue(descriptor.resolutionSource.c_str()));
        result.SetMember("fallback", RE::GFxValue(descriptor.fallback.c_str()));
        result.SetMember("deviceProfile", RE::GFxValue(descriptor.deviceProfile.c_str()));
        result.SetMember("manifestEpoch", RE::GFxValue(static_cast<double>(descriptor.manifestEpoch)));
        result.SetMember("promptScopeRevision", RE::GFxValue(static_cast<double>(descriptor.promptScopeRevision)));

        logger::info(
            "[DualPad][PromptAdapter] GameDelegate descriptor action={} requestedContext={} ok={} token={} status={} resolvedContext={} resolvedSet={} scopeRevision={} manifestEpoch={}",
            actionId,
            contextName,
            descriptor.ok,
            descriptor.ok ? descriptor.buttonArtToken : std::string("<none>"),
            descriptor.failureReason,
            descriptor.resolvedContextId,
            descriptor.resolvedActionSetId,
            descriptor.promptScopeRevision,
            descriptor.manifestEpoch);

        args.Respond(result);
    }

    bool ScaleformPromptAdapter::AttachToMenu(std::string_view menuName)
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        auto menu = ui->GetMenu(menuName);
        if (!menu || !menu->fxDelegate) {
            return false;
        }

        const auto delegateKey = reinterpret_cast<std::uintptr_t>(menu->fxDelegate.get());
        {
            std::scoped_lock lock(_mutex);
            if (_registeredDelegates.contains(delegateKey)) {
                return true;
            }
            menu->fxDelegate->RegisterHandler(this);
            _registeredDelegates.insert(delegateKey);
        }

        logger::info("[DualPad][PromptAdapter] Registered GameDelegate prompt handler for {}", menuName);
        return true;
    }
}
