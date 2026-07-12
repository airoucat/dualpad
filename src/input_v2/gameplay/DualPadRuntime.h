#pragma once

#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/gameplay/GameplayPresentationPublisher.h"
#include "input_v2/gameplay/GameplayProjectionFrame.h"
#include "input_v2/gameplay/PollOutputAdapter.h"
#include "input_v2/gameplay/RuntimeDiagnostics.h"
#include "input_v2/gameplay/RuntimeFrameEnvelope.h"
#include "input_v2/gameplay/RuntimeInputPublication.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/presentation/PresentationProjection.h"

#include <cstdint>
#include <optional>
#include <string>

namespace dualpad::input_v2::gameplay
{
    struct DualPadRuntimeInput
    {
        actions::KernelFrame kernel{};
        actions::ResolvedActionFrame resolved{};
        GameplayPolicy policy{};
        GameplayRecoveryInput recovery{};
        std::optional<CurrentCycleGatePlan> currentCyclePlan;
        std::optional<CurrentCycleAdapterAudit> currentCycleAudit;
        std::optional<CurrentCycleCallbackEvidence> currentCycleEvidence;
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        std::uint64_t orderedCutoffSeq{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        RuntimeHealthReasonMask runtimeHealthReasons{ RuntimeHealthMask(RuntimeHealthReason::None) };
        std::uint64_t outputTick{ 0 };
        dualpad::input::InputContext legacyContext{ dualpad::input::InputContext::Gameplay };
        std::uint32_t legacyContextEpoch{ 1 };
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

    GameplayPolicy BuildGameplayPolicyFromFacts(
        const ingress::FactFrame& facts,
        bool gameplayContext,
        const GameplayRecoveryInput& recovery);

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

        presentation::PublishedGameplayPresentation GetPublishedGameplayPresentation() const;
        GameplayProjectionFrame GetLastProjectionFrame() const;
        RuntimeDebugSnapshot GetLastDebugSnapshot() const;
        void ResetOnOwnerTick();
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
        void ResetMutableState();

        GameplayProjectionFrame _lastProjectionFrame{};
        ChannelArbitrationStateSet _channelArbitration{};
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
