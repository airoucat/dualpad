#pragma once

#include "input/ActionDispatcher.h"
#include "input/Action.h"
#include "input/backend/FrameActionPlan.h"
#include "input/backend/FrameActionPlanner.h"
#include "input/backend/NativeStateBackend.h"
#include "input/mapping/BindingResolver.h"
#include "input/injection/CompatibilityInputInjector.h"
#include "input/injection/NativeInputInjector.h"
#include "input/injection/PadEventSnapshot.h"
#include "input/injection/SyntheticStateReducer.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace dualpad::input
{
    class PadEventSnapshotProcessor
    {
    public:
        static PadEventSnapshotProcessor& GetSingleton();

        void Process(const PadEventSnapshot& snapshot);
        void ResetState();
        void PrependInjectedInputEvents(RE::InputEvent*& head);
        std::size_t PrependInjectedInputEventsUsingQueueCache(RE::InputEvent*& head);
        std::size_t PrependInjectedInputQueueEvents(RE::InputEvent*& head, RE::InputEvent*& tail);
        std::size_t GetPendingInjectedButtonCount() const;
        void DiscardPendingInjectedButtonEvents();
        void ReleaseInjectedInputEvents();
        std::size_t FlushInjectedInputQueue();

    private:
        struct ActiveButtonAction
        {
            bool active{ false };
            std::string actionId{};
            std::uint64_t lastObservedHoldBucket{ std::numeric_limits<std::uint64_t>::max() };
        };

        PadEventSnapshotProcessor();

        BindingResolver _bindingResolver{};
        CompatibilityInputInjector _compatibilityInjector{};
        NativeInputInjector _nativeInjector{};
        ActionDispatcher _actionDispatcher;
        SyntheticStateReducer _stateReducer{};
        std::uint64_t _lastProcessedSequence{ 0 };
        std::uint32_t _blockedSourceButtons{ 0 };
        std::array<ActiveButtonAction, 32> _activeButtonActions{};
        bool _rawSprintObservedDown{ false };
        std::uint64_t _rawSprintHoldBucket{ std::numeric_limits<std::uint64_t>::max() };

        std::uint32_t DispatchPadEvents(const PadEventBuffer& events, InputContext context);
        void UpdateActiveButtonActions(const SyntheticPadFrame& frame, InputContext context);
        void ObserveSprintState(
            std::string_view phase,
            const SyntheticPadFrame& frame,
            const SyntheticButtonState& button,
            ActionDispatchTarget target,
            std::uint32_t sourceCode) const;
        void ObserveRawSprintCompatibilityState(const SyntheticPadFrame& frame, std::uint32_t handledButtons);
        void ResetShadowPlanning();
        void BeginShadowPlanning(InputContext context);
        void FinishShadowPlanning();

        backend::FrameActionPlanner _shadowPlanner{};
        backend::FrameActionPlan _shadowPlan{};
        backend::NativeStateBackend _shadowNativeState{};
    };
}
