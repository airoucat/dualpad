#include "pch.h"
#include "input/ContextEventSink.h"
#include "input/glyph/ScaleformGlyphBridge.h"
#include "input_v2/context/ContextRefreshTick.h"
#include "input_v2/menu/UiMenuObserver.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    ContextEventSink& ContextEventSink::GetSingleton()
    {
        static ContextEventSink instance;
        return instance;
    }

    void ContextEventSink::Register()
    {
        logger::info("[DualPad][ContextSink] Registering event listeners");

        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
            (void)dualpad::input_v2::menu::UiMenuObserver::GetSingleton().QueueCaptureOnUiThread();
            logger::info("[DualPad][ContextSink] Menu event listener registered");
        }
        else {
            logger::error("[DualPad][ContextSink] Failed to get UI singleton");
        }

        auto* combatSource = RE::ScriptEventSourceHolder::GetSingleton();
        if (combatSource) {
            combatSource->AddEventSink<RE::TESCombatEvent>(this);
            logger::info("[DualPad][ContextSink] Combat event listener registered");
        }
        else {
            logger::warn("[DualPad][ContextSink] Failed to get combat event source");
        }

        logger::info("[DualPad][ContextSink] Runtime owner consumes immutable snapshots captured by UI tasks");
        logger::info("[DualPad][ContextSink] All event listeners registered");
    }

    void ContextEventSink::Unregister()
    {
        logger::info("[DualPad][ContextSink] Unregistering event listeners");

        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            ui->RemoveEventSink<RE::MenuOpenCloseEvent>(this);
        }

        auto* combatSource = RE::ScriptEventSourceHolder::GetSingleton();
        if (combatSource) {
            combatSource->RemoveEventSink<RE::TESCombatEvent>(this);
        }

        logger::info("[DualPad][ContextSink] All event listeners unregistered");
    }

    // Menu events publish a partial transition fact immediately, then defer live
    // RE::UI sampling to the verified UI task queue.
    RE::BSEventNotifyControl ContextEventSink::ProcessEvent(
        const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
    {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        logger::debug(
            "[DualPad][MenuEvent] name={} opening={}",
            event->menuName.c_str(),
            event->opening);

        auto& observer = dualpad::input_v2::menu::UiMenuObserver::GetSingleton();
        observer.MarkMenuEvent(event->menuName.c_str(), event->opening);
        (void)observer.QueueCaptureOnUiThread();

        auto& glyphBridge = dualpad::input::glyph::ScaleformGlyphBridge::GetSingleton();
        if (event->opening) {
            glyphBridge.OnMenuOpened(event->menuName.c_str());
        }
        else {
            glyphBridge.OnMenuClosed(event->menuName.c_str());
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    // Combat events patch over gameplay states that are not visible from menu polling.
    RE::BSEventNotifyControl ContextEventSink::ProcessEvent(
        const RE::TESCombatEvent* event,
        RE::BSTEventSource<RE::TESCombatEvent>*)
    {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // Ignore combat updates that do not involve the player.
        if (event->actor.get() != player && event->targetActor.get() != player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (event->newState == RE::ACTOR_COMBAT_STATE::kCombat) {

            logger::info("[DualPad][ContextSink] Player entered combat");
            dualpad::input_v2::context::ContextRefreshTick::GetSingleton().MarkCombatEvent(true);
        }
        else if (event->newState == RE::ACTOR_COMBAT_STATE::kNone) {

            logger::info("[DualPad][ContextSink] Player left combat");
            dualpad::input_v2::context::ContextRefreshTick::GetSingleton().MarkCombatEvent(false);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
