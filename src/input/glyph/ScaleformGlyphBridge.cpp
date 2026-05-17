#include "pch.h"

#include "input/glyph/ScaleformGlyphBridge.h"

#include "input/glyph/GlyphResolutionCompat.h"

#include <RE/F/FxDelegateArgs.h>
#include <RE/F/FxResponseArgs.h>
#include <RE/M/MainMenu.h>
#include <RE/U/UI.h>

namespace logger = SKSE::log;

namespace dualpad::input::glyph
{
    ScaleformGlyphBridge& ScaleformGlyphBridge::GetSingleton()
    {
        static ScaleformGlyphBridge instance;
        return instance;
    }

    void ScaleformGlyphBridge::RegisterInitialMenus()
    {
        if (auto* task = SKSE::GetTaskInterface(); task) {
            task->AddUITask([] {
                auto& bridge = ScaleformGlyphBridge::GetSingleton();
                bridge.AttachToMenu(RE::MainMenu::MENU_NAME);
            });
        }
    }

    void ScaleformGlyphBridge::OnMenuOpened(std::string_view menuName)
    {
        const auto menu = std::string(menuName);
        AttachToMenu(menu);

        if (auto* task = SKSE::GetTaskInterface(); task) {
            task->AddUITask([menu] {
                ScaleformGlyphBridge::GetSingleton().AttachToMenu(menu);
            });
        }
    }

    void ScaleformGlyphBridge::Accept(CallbackProcessor* processor)
    {
        if (!processor) {
            return;
        }

        processor->Process("DualPad_GetActionGlyphToken", HandleGetActionGlyphToken);
        processor->Process("DualPad_GetActionGlyph", HandleGetActionGlyph);
    }

    void ScaleformGlyphBridge::HandleGetActionGlyphToken(const RE::FxDelegateArgs& args)
    {
        RE::FxResponseArgs<1> response;
        response.Add(RE::GFxValue(""));

        const char* actionId = nullptr;
        const char* contextName = nullptr;
        if (args.GetArgCount() < 2 || !args[0].IsString() || !args[1].IsString()) {
            args.Respond(response);
            return;
        }
        actionId = args[0].GetString();
        contextName = args[1].GetString();
        if (!actionId || !contextName) {
            args.Respond(response);
            return;
        }

        const auto resolution = ResolveActionGlyphCompat(actionId, contextName);
        if (resolution.ok) {
            response = RE::FxResponseArgs<1>();
            response.Add(RE::GFxValue(resolution.token.c_str()));
        }

        logger::info(
            "[DualPad][GlyphBridge] GameDelegate action={} requestedContext={} resolvedContext={} token={} status={} fallbackKind={} fallbackReason={} candidateCount={} reverseLookupAmbiguous={} triggerType={} triggerCode={}",
            actionId,
            contextName,
            resolution.resolvedContextName,
            resolution.ok ? resolution.token : std::string("<none>"),
            ToString(resolution.status),
            ToString(resolution.fallbackKind),
            ToString(resolution.fallbackReason),
            resolution.candidateCount,
            resolution.reverseLookupAmbiguous,
            dualpad::input::ToString(resolution.resolvedTriggerType),
            resolution.resolvedTriggerCode);

        args.Respond(response);
    }

    void ScaleformGlyphBridge::HandleGetActionGlyph(const RE::FxDelegateArgs& args)
    {
        RE::GFxValue emptyResult;
        emptyResult.SetNull();

        const char* actionId = nullptr;
        const char* contextName = nullptr;
        if (args.GetArgCount() < 2 || !args[0].IsString() || !args[1].IsString()) {
            args.Respond(emptyResult);
            return;
        }
        actionId = args[0].GetString();
        contextName = args[1].GetString();
        if (!actionId || !contextName) {
            args.Respond(emptyResult);
            return;
        }
        const auto resolution = ResolveActionGlyphCompat(actionId, contextName);

        auto* movie = args.GetMovie();
        if (!movie) {
            logger::info(
                "[DualPad][GlyphBridge] GameDelegate descriptor action={} context={} result=<null movie>",
                actionId,
                contextName);
            args.Respond(emptyResult);
            return;
        }

        RE::GFxValue descriptor;
        movie->CreateObject(&descriptor);

        RE::GFxValue okValue(false);
        RE::GFxValue tokenValue("");
        RE::GFxValue semanticIdValue(actionId);
        RE::GFxValue contextValue(contextName);

        if (resolution.ok) {
            okValue = RE::GFxValue(true);
            tokenValue = RE::GFxValue(resolution.token.c_str());
        }

        descriptor.SetMember("ok", okValue);
        descriptor.SetMember("buttonArtToken", tokenValue);
        descriptor.SetMember("semanticId", semanticIdValue);
        descriptor.SetMember("contextName", contextValue);

        logger::info(
            "[DualPad][GlyphBridge] GameDelegate descriptor action={} requestedContext={} resolvedContext={} ok={} buttonArtToken={} status={} fallbackKind={} fallbackReason={} candidateCount={} reverseLookupAmbiguous={} triggerType={} triggerCode={}",
            actionId,
            contextName,
            resolution.resolvedContextName,
            resolution.ok,
            resolution.ok ? resolution.token : std::string("<none>"),
            ToString(resolution.status),
            ToString(resolution.fallbackKind),
            ToString(resolution.fallbackReason),
            resolution.candidateCount,
            resolution.reverseLookupAmbiguous,
            dualpad::input::ToString(resolution.resolvedTriggerType),
            resolution.resolvedTriggerCode);

        args.Respond(descriptor);
    }

    bool ScaleformGlyphBridge::AttachToMenu(std::string_view menuName)
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

        logger::info("[DualPad][GlyphBridge] Registered GameDelegate glyph handler for {}", menuName);
        return true;
    }

}
