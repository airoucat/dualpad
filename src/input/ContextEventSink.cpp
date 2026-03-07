#include "pch.h"
#include "input/ContextEventSink.h"
#include "input/InputContext.h"
#include <SKSE/SKSE.h>

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

        StartPerFrameUpdate();

        logger::info("[DualPad][ContextSink] All event listeners registered");
    }

    void ContextEventSink::Unregister()
    {
        logger::info("[DualPad][ContextSink] Unregistering event listeners");

        StopPerFrameUpdate();

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

    // Menu events own the authoritative UI context transitions.
    RE::BSEventNotifyControl ContextEventSink::ProcessEvent(
        const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
    {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto& contextMgr = ContextManager::GetSingleton();

        if (event->opening) {
            contextMgr.OnMenuOpen(event->menuName.c_str());
        }
        else {
            contextMgr.OnMenuClose(event->menuName.c_str());
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

        auto& contextMgr = ContextManager::GetSingleton();

        if (event->newState == RE::ACTOR_COMBAT_STATE::kCombat) {

            logger::info("[DualPad][ContextSink] Player entered combat");
            contextMgr.SetContext(InputContext::Combat);
        }
        else if (event->newState == RE::ACTOR_COMBAT_STATE::kNone) {

            logger::info("[DualPad][ContextSink] Player left combat");
            contextMgr.SetContext(InputContext::Gameplay);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void ContextEventSink::StartPerFrameUpdate()
    {
        if (_updateRunning.exchange(true)) {
            return;
        }

        _updateThread = std::jthread([this](std::stop_token) {
            PerFrameUpdateLoop();
            });

        logger::info("[DualPad][ContextSink] Per-frame update started");
    }

    void ContextEventSink::StopPerFrameUpdate()
    {
        _updateRunning.store(false);

        if (_updateThread.joinable()) {
            _updateThread.request_stop();
            _updateThread.join();
        }

        logger::info("[DualPad][ContextSink] Per-frame update stopped");
    }

    void ContextEventSink::PerFrameUpdateLoop()
    {
        using namespace std::chrono_literals;

        while (_updateRunning.load()) {
            // Gameplay-only states such as sneak, riding, or death are sampled here.
            ContextManager::GetSingleton().UpdateGameplayContext();

            std::this_thread::sleep_for(16ms);
        }
    }
}
