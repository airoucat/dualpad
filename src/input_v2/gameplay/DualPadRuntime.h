#pragma once

#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/gameplay/GameplayPresentationPublisher.h"
#include "input_v2/gameplay/GameplayProjectionFrame.h"
#include "input_v2/gameplay/PollOutputAdapter.h"
#include "input_v2/gameplay/RuntimeDiagnostics.h"
#include "input_v2/gameplay/RuntimeFrameEnvelope.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/presentation/PresentationProjection.h"

#include <cstdint>
#include <string>

namespace dualpad::input_v2::gameplay
{
    struct DualPadRuntimeInput
    {
        actions::KernelFrame kernel{};
        actions::ResolvedActionFrame resolved{};
        GameplayPolicy policy{};
        GameplayRecoveryInput recovery{};
        RuntimeHealthReasonMask runtimeHealthReasons{ RuntimeHealthMask(RuntimeHealthReason::None) };
        std::uint64_t outputTick{ 0 };
        dualpad::input::InputContext legacyContext{ dualpad::input::InputContext::Gameplay };
        std::string runtimeHealthDebugReason;
    };

    struct DualPadRuntimeResult
    {
        GameplayProjectionFrame projectionFrame{};
        PollOutputApplyResult output{};
        presentation::PublishedGameplayPresentation gameplayPresentation{};
        RuntimeHealthReasonMask runtimeHealthReasons{ RuntimeHealthMask(RuntimeHealthReason::None) };
        std::string runtimeHealthDebugReason;

        [[nodiscard]] bool RuntimeHealthDegraded() const
        {
            return runtimeHealthReasons != RuntimeHealthMask(RuntimeHealthReason::None);
        }
    };

    class DualPadRuntime
    {
    public:
        static DualPadRuntime& GetSingleton();
        static bool LiveCoordinatorPresentationAuthorityReachable();

        DualPadRuntimeResult ProcessAssembledFrame(const ingress::AssembledFactFrame& frame);
        DualPadRuntimeResult ProcessAssembledFrameForTests(
            const ingress::AssembledFactFrame& frame,
            IPollOutputExecutor& executor);
        DualPadRuntimeResult ProcessGameplayFrame(const DualPadRuntimeInput& input);
        DualPadRuntimeResult ProcessGameplayFrameForTests(
            const DualPadRuntimeInput& input,
            IPollOutputExecutor& executor);

        presentation::PublishedGameplayPresentation PublishGameplayPresentation(
            const GameplayProjectionFrame& frame,
            std::uint64_t tick,
            bool outputApplySucceeded);

        const presentation::PublishedGameplayPresentation& GetPublishedGameplayPresentation() const;
        const GameplayProjectionFrame& GetLastProjectionFrame() const;
        const RuntimeDebugSnapshot& GetLastDebugSnapshot() const;
        void ResetForTests();

    private:
        FrameRuntimeEnvelope BindRuntimeEnvelope(const ingress::AssembledFactFrame& frame) const;
        DualPadRuntimeInput BuildStableRuntimeInput(const FrameRuntimeEnvelope& envelope);
        DualPadRuntimeResult ProcessTransitionFrame(const ingress::AssembledFactFrame& frame);
        void PublishStablePresentationSurface(
            const FrameRuntimeEnvelope& envelope,
            const DualPadRuntimeResult& result);
        void PublishRuntimeDebugSnapshot(
            const ingress::AssembledFactFrame& frame,
            const DualPadRuntimeResult& result);

        DualPadRuntimeResult ProcessGameplayFrameWithExecutor(
            const DualPadRuntimeInput& input,
            IPollOutputExecutor& executor);

        GameplayProjectionFrame _lastProjectionFrame{};
        GameplayRecoveryInput _pendingRecovery{};
        bool _hasPendingRecovery{ false };
        actions::InteractionStateStore _interactionState{};
        actions::InteractionEngine _interactionEngine{};
        GameplayPresentationPublisher _presentationPublisher{};
        presentation::PresentationProjection _presentationProjection{};
        PollOutputAdapter _pollOutputAdapter{};
        RuntimeDebugSnapshot _lastDebugSnapshot{};
        RuntimeDiagnosticsLogState _diagnosticsLogState{};
    };
}
