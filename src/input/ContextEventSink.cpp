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

        // 注册菜单事件
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
            logger::info("[DualPad][ContextSink] Menu event listener registered");
        }
        else {
            logger::error("[DualPad][ContextSink] Failed to get UI singleton");
        }

        // 注册战斗事件
        auto* combatSource = RE::ScriptEventSourceHolder::GetSingleton();
        if (combatSource) {
            combatSource->AddEventSink<RE::TESCombatEvent>(this);
            logger::info("[DualPad][ContextSink] Combat event listener registered");
        }
        else {
            logger::warn("[DualPad][ContextSink] Failed to get combat event source");
        }

        // 启动每帧更新
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

    // === 菜单事件 ===
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

    // === 战斗事件 ===
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

        // 检查是否是玩家的战斗事件
        // 使用 .get() 获取原始指针
        if (event->actor.get() != player && event->targetActor.get() != player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto& contextMgr = ContextManager::GetSingleton();

        if (event->newState == RE::ACTOR_COMBAT_STATE::kCombat) {
            // 进入战斗
            logger::info("[DualPad][ContextSink] Player entered combat");
            contextMgr.SetContext(InputContext::Combat);
        }
        else if (event->newState == RE::ACTOR_COMBAT_STATE::kNone) {
            // 退出战斗
            logger::info("[DualPad][ContextSink] Player left combat");
            contextMgr.SetContext(InputContext::Gameplay);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    // === 每帧更新 ===
    void ContextEventSink::StartPerFrameUpdate()
    {
        if (_updateRunning.exchange(true)) {
            return;
        }

        _updateThread = std::jthread([this](std::stop_token) {  // 不使用参数名
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
            // 更新游戏状态上下文
            ContextManager::GetSingleton().UpdateGameplayContext();

            // 60 FPS
            std::this_thread::sleep_for(16ms);
        }
    }
}