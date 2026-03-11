#pragma once

#include <RE/Skyrim.h>

namespace dualpad::input
{
    class InputFramePump final :
        public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static InputFramePump& GetSingleton();

        void Register();
        void Unregister();

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* event,
            RE::BSTEventSource<RE::InputEvent*>* source) override;

    private:
        InputFramePump() = default;

        bool _registered{ false };
    };
}
