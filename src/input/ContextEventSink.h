#pragma once
#include <RE/Skyrim.h>
#include <atomic>
#include <thread>

namespace dualpad::input
{
    // 统一的上下文事件监听器
    class ContextEventSink :
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
        public RE::BSTEventSink<RE::TESCombatEvent>
    {
    public:
        static ContextEventSink& GetSingleton();

        // 注册所有事件监听
        void Register();

        // 停止监听
        void Unregister();

        // 菜单事件
        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

        // 战斗事件
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCombatEvent* event,
            RE::BSTEventSource<RE::TESCombatEvent>*) override;

    private:
        ContextEventSink() = default;

        std::atomic_bool _updateRunning{ false };
        std::jthread _updateThread;

        void StartPerFrameUpdate();
        void StopPerFrameUpdate();
        void PerFrameUpdateLoop();
    };
}