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
        struct RecoveryBaseline
        {
            bool valid{ false };
            InputContext context{ InputContext::Gameplay };
            std::uint32_t contextEpoch{ 0 };
            std::uint32_t downMask{ 0 };
            std::uint64_t sequence{ 0 };
        };

        PadEventSnapshotProcessor();
        void ResetAllState();
        void ResyncNativeState();
        void ResetRecoveryBaseline();
        void CommitCleanRecoveryBaseline(
            const SyntheticPadFrame& frame,
            InputContext context,
            std::uint32_t contextEpoch,
            std::uint64_t sequence);

        BindingResolver _bindingResolver{};
        ActionDispatcher _actionDispatcher;
        SyntheticStateReducer _stateReducer{};
        backend::ActionLifecycleCoordinator _lifecycleCoordinator{};
        SourceBlockCoordinator _sourceBlockCoordinator{};
        std::uint64_t _lastProcessedSequence{ 0 };

        std::uint32_t CollectPlannedActions(
            const PadEventBuffer& events,
            InputContext context,
            std::uint32_t contextEpoch);
        std::uint32_t RecoverMissingPressStateAfterResync(
            const SyntheticPadFrame& frame,
            InputContext context,
            std::uint32_t contextEpoch,
            bool crossContextMismatch,
            bool hardResync,
            const PadState& state,
            PadEventBuffer& events);
        void CollectLifecycleActions(
            const SyntheticPadFrame& frame,
            InputContext context,
            std::uint32_t contextEpoch);
        void DispatchPlannedActions();
        void ResetFramePlanning();
        void BeginFramePlanning(InputContext context, std::uint32_t contextEpoch);
        void FinishFramePlanning(const SyntheticPadFrame& frame, InputContext context);

        backend::FrameActionPlanner _planner{};
        backend::FrameActionPlan _framePlan{};
        RecoveryBaseline _cleanRecoveryBaseline{};
    };
}
