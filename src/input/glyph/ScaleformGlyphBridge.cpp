#include "pch.h"

#include "input/glyph/ScaleformGlyphBridge.h"

#include "input/BindingManager.h"
#include "input/InputContextNames.h"
#include "input/mapping/PadEvent.h"

#include <RE/F/FxDelegateArgs.h>
#include <RE/F/FxResponseArgs.h>
#include <RE/M/MainMenu.h>
#include <RE/U/UI.h>

namespace logger = SKSE::log;

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

    }

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

        const auto context = ParseInputContextName(contextName).value_or(InputContext::Menu);
        const auto token = ResolveActionToken(actionId, context);
        if (token) {
            response = RE::FxResponseArgs<1>();
            response.Add(RE::GFxValue(token->c_str()));
            logger::info(
                "[DualPad][GlyphBridge] GameDelegate action={} context={} token={}",
                actionId,
                contextName,
                *token);
        }
        else {
            logger::info(
                "[DualPad][GlyphBridge] GameDelegate action={} context={} token=<none>",
                actionId,
                contextName);
        }

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
        const auto context = ParseInputContextName(contextName).value_or(InputContext::Menu);

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

        const auto token = ResolveActionToken(actionId, context);
        if (token) {
            okValue = RE::GFxValue(true);
            tokenValue = RE::GFxValue(token->c_str());
        }

        descriptor.SetMember("ok", okValue);
        descriptor.SetMember("buttonArtToken", tokenValue);
        descriptor.SetMember("semanticId", semanticIdValue);
        descriptor.SetMember("contextName", contextValue);

        if (token) {
            logger::info(
                "[DualPad][GlyphBridge] GameDelegate descriptor action={} context={} ok=true buttonArtToken={}",
                actionId,
                contextName,
                *token);
        }
        else {
            logger::info(
                "[DualPad][GlyphBridge] GameDelegate descriptor action={} context={} ok=false buttonArtToken=<none>",
                actionId,
                contextName);
        }

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

    std::optional<std::string> ScaleformGlyphBridge::ResolveActionToken(std::string_view actionId, InputContext context)
    {
        auto& bindingManager = BindingManager::GetSingleton();

        if (auto trigger = bindingManager.GetTriggerForAction(actionId, context)) {
            if (auto token = TriggerToButtonArtToken(*trigger)) {
                return token;
            }
        }

        if (context != InputContext::Menu) {
            if (auto trigger = bindingManager.GetTriggerForAction(actionId, InputContext::Menu)) {
                if (auto token = TriggerToButtonArtToken(*trigger)) {
                    return token;
                }
            }
        }

        return std::nullopt;
    }

    std::optional<std::string> ScaleformGlyphBridge::TriggerToButtonArtToken(const Trigger& trigger)
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
}
