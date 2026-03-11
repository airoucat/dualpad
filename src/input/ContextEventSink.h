#pragma once
#include <RE/Skyrim.h>

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
    };
}
