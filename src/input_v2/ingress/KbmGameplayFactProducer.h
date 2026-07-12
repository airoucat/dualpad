#pragma once

#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/KbmGameplayFacts.h"

#include <cstdint>
#include <vector>

namespace dualpad::input_v2::ingress
{
    class KbmGameplayFactProducer
    {
    public:
        KbmGameplayIngressBatchDraft BuildIngressBatch(
            const KbmObservedBatch& observed,
            const KbmBindingSnapshot& bindings,
            const context::ResolvedContextSnapshot& context,
            std::uint64_t ownerNowUs);

        void RegisterSyntheticSuppression(const SyntheticKeyboardSuppressionToken& token);
        void EnterQuarantine(
            InputResetReasonMask reasons,
            std::uint32_t contextRevision,
            std::uint32_t controlMapRevision);
        void ResetSyntheticSuppression(InputResetReasonMask reasons) noexcept;

    private:
        [[nodiscard]] bool ConsumeExactSyntheticReceipt(
            const KbmObservedEventDraft& event,
            std::uint32_t contextRevision,
            std::uint64_t ownerNowUs);
        [[nodiscard]] KbmGameplayCurrentFacts BuildCurrentFacts(
            const KbmBindingSnapshot& bindings) const;
        void AppendMappedEdges(
            KbmGameplayIngressBatchDraft& batch,
            const KbmObservedEventDraft& event,
            const KbmBindingSnapshot& bindings,
            KbmEdgeOrigin origin) const;
        void AppendSourceActivity(
            KbmGameplayIngressBatchDraft& batch,
            const KbmObservedEventDraft& event) const;
        void ApplyTrustedRawState(
            KbmGameplayIngressBatchDraft& batch,
            const KbmRawCurrentState& raw,
            const KbmBindingSnapshot& bindings);

        KbmPhysicalLedger _physical{};
        std::vector<SyntheticKeyboardSuppressionToken> _suppressionTokens;
        std::uint64_t _bindingGeneration{ 0 };
        std::uint32_t _contextRevision{ 0 };
        std::uint32_t _controlMapRevision{ 0 };
        std::uint64_t _physicalEpoch{ 0 };
        std::uint64_t _lastPhysicalMouseMoveOwnerUs{ 0 };
        bool _initialized{ false };
    };
}
