#pragma once

#include "input/ActionDispatcher.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/ActionLifecycleCoordinator.h"
#include "input/backend/FrameActionPlan.h"
#include "input/backend/FrameActionPlanner.h"
#include "input/backend/NativeControlCode.h"
#include "input/mapping/BindingResolver.h"
#include "input/injection/PadEventSnapshot.h"
#include "input/injection/SourceBlockCoordinator.h"
#include "input/injection/SyntheticStateReducer.h"

#include <cstdint>
#include <limits>

namespace dualpad::input
{
    class PadEventSnapshotProcessor
    {
    public:
        static PadEventSnapshotProcessor& GetSingleton();

        void Process(const PadEventSnapshot& snapshot);
        void ResetState();

    private:
        PadEventSnapshotProcessor();

        BindingResolver _bindingResolver{};
        ActionDispatcher _actionDispatcher;
        SyntheticStateReducer _stateReducer{};
        backend::ActionLifecycleCoordinator _lifecycleCoordinator{};
        SourceBlockCoordinator _sourceBlockCoordinator{};
        std::uint64_t _lastProcessedSequence{ 0 };

        std::uint32_t CollectPlannedActions(const PadEventBuffer& events, InputContext context);
        void CollectLifecycleActions(const SyntheticPadFrame& frame, InputContext context);
        void DispatchPlannedActions();
        void ResetFramePlanning();
        void BeginFramePlanning(InputContext context);
        void FinishFramePlanning(const SyntheticPadFrame& frame, InputContext context);

        backend::FrameActionPlanner _planner{};
        backend::FrameActionPlan _framePlan{};
    };
}
