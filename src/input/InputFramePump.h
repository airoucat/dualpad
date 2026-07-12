#pragma once

#include "input/injection/SkyrimKbmInputAdapter.h"
#include "input/injection/SkyrimCurrentCycleEventAdapter.h"
#include "input_v2/ingress/KbmGameplayFactProducer.h"

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
        std::uint64_t NextEventBatchToken();

        bool _registered{ false };
        std::uint64_t _eventBatchToken{ 0 };
        SkyrimKbmInputAdapter _skyrimKbmAdapter{};
        SkyrimCurrentCycleEventAdapter _currentCycleAdapter{};
        input_v2::ingress::KbmGameplayFactProducer _kbmProducer{};
    };
}
