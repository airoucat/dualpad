#pragma once
#include <RE/Skyrim.h>
#include <atomic>
#include <thread>

namespace dualpad::input
{
    // Forwards Skyrim UI and combat events into the local context model.
    class ContextEventSink :
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
        public RE::BSTEventSink<RE::TESCombatEvent>
    {
    public:
        static ContextEventSink& GetSingleton();

        void Register();

        void Unregister();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCombatEvent* event,
            RE::BSTEventSource<RE::TESCombatEvent>*) override;

    private:
        ContextEventSink() = default;

        std::atomic_bool _updateRunning{ false };
        std::jthread _updateThread;

        // Polls gameplay-only states that are not exposed by menu events.
        void StartPerFrameUpdate();
        void StopPerFrameUpdate();
        void PerFrameUpdateLoop();
    };
}
