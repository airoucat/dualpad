#include "pch.h"

#include "input_v2/ingress/KbmGameplayFactProducer.h"

#include <algorithm>

namespace dualpad::input_v2::ingress
{
    namespace
    {
        void CopySet(KbmPhysicalCodeSet& destination, const KbmPhysicalCodeSet& source)
        {
            destination = source;
        }

        void UnionInto(KbmPhysicalCodeSet& destination, const KbmPhysicalCodeSet& source)
        {
            for (std::size_t index = 0; index < source.count; ++index) {
                (void)destination.Insert(source.values[index]);
            }
        }
    }

    KbmGameplayIngressBatchDraft KbmGameplayFactProducer::BuildIngressBatch(
        const KbmObservedBatch& observed,
        const KbmBindingSnapshot& bindings,
        const context::ResolvedContextSnapshot& context,
        std::uint64_t ownerNowUs)
    {
        KbmGameplayIngressBatchDraft batch{
            .ownerTickToken = observed.ownerTickToken,
            .eventBatchToken = observed.eventBatchToken,
            .bindingGeneration = bindings.generation,
            .controlMapFingerprint = bindings.controlMapFingerprint
        };

        const bool trustedRaw = observed.rawCurrent.complete &&
            observed.rawCurrent.physicalOnlyProvenance;
        if (!_initialized) {
            _initialized = true;
            _bindingGeneration = bindings.generation;
            _contextRevision = context.contextRevision;
            _controlMapRevision = bindings.controlMapRevision;
            _physicalEpoch = 1;
            if (trustedRaw) {
                CopySet(_physical.downCodes, observed.rawCurrent.downCodes);
            }
        } else if (_bindingGeneration != bindings.generation ||
            _contextRevision != context.contextRevision ||
            _controlMapRevision != bindings.controlMapRevision) {
            UnionInto(_physical.quarantineCodes, _physical.downCodes);
            if (trustedRaw) {
                UnionInto(_physical.quarantineCodes, observed.rawCurrent.downCodes);
                CopySet(_physical.downCodes, observed.rawCurrent.downCodes);
            }
            ++_physicalEpoch;
            _bindingGeneration = bindings.generation;
            _contextRevision = context.contextRevision;
            _controlMapRevision = bindings.controlMapRevision;
        }

        for (const auto& event : observed.events) {
            if (ConsumeExactSyntheticReceipt(event, context.contextRevision, ownerNowUs)) {
                continue;
            }

            const bool quarantined = _physical.quarantineCodes.Contains(event.physical);
            if (quarantined) {
                if (event.phase == KbmEdgePhase::Release) {
                    (void)_physical.downCodes.Erase(event.physical);
                    (void)_physical.quarantineCodes.Erase(event.physical);
                    AppendMappedEdges(batch, event, bindings, KbmEdgeOrigin::Reconciled);
                } else if (event.phase == KbmEdgePhase::Press && event.initialPress) {
                    (void)_physical.quarantineCodes.Erase(event.physical);
                    (void)_physical.downCodes.Insert(event.physical);
                    AppendMappedEdges(batch, event, bindings, KbmEdgeOrigin::Physical);
                    AppendSourceActivity(batch, event);
                }
                continue;
            }

            switch (event.phase) {
            case KbmEdgePhase::Press:
                (void)_physical.downCodes.Insert(event.physical);
                if (event.initialPress) {
                    AppendMappedEdges(batch, event, bindings, KbmEdgeOrigin::Physical);
                    AppendSourceActivity(batch, event);
                }
                break;
            case KbmEdgePhase::Release:
                (void)_physical.downCodes.Erase(event.physical);
                AppendMappedEdges(batch, event, bindings, KbmEdgeOrigin::Physical);
                break;
            case KbmEdgePhase::MouseDelta:
                if (event.deltaX != 0 || event.deltaY != 0) {
                    _lastPhysicalMouseMoveOwnerUs = ownerNowUs;
                    AppendMappedEdges(batch, event, bindings, KbmEdgeOrigin::Physical);
                    AppendSourceActivity(batch, event);
                }
                break;
            case KbmEdgePhase::ReconciledRelease:
                (void)_physical.downCodes.Erase(event.physical);
                AppendMappedEdges(batch, event, bindings, KbmEdgeOrigin::Reconciled);
                break;
            }
        }

        if (trustedRaw) {
            ApplyTrustedRawState(batch, observed.rawCurrent, bindings);
        }

        _physical.physicalEpoch = _physicalEpoch;
        _physical.complete = observed.eventListComplete || trustedRaw;
        batch.completeCurrent = BuildCurrentFacts(bindings);
        batch.completeCurrent.complete = _physical.complete;
        batch.physical = _physical;
        batch.lastPhysicalMouseMoveOwnerUs = _lastPhysicalMouseMoveOwnerUs;
        batch.baseline = _physical.QuarantineDrained() ?
            KbmBaselineState::Clean : KbmBaselineState::MappingRearmRequired;
        return batch;
    }

    void KbmGameplayFactProducer::RegisterSyntheticSuppression(
        const SyntheticKeyboardSuppressionToken& token)
    {
        if (token.remainingMatches == 0) {
            return;
        }
        _suppressionTokens.push_back(token);
    }

    void KbmGameplayFactProducer::EnterQuarantine(
        InputResetReasonMask,
        std::uint32_t contextRevision,
        std::uint32_t controlMapRevision)
    {
        UnionInto(_physical.quarantineCodes, _physical.downCodes);
        ++_physicalEpoch;
        _contextRevision = contextRevision;
        _controlMapRevision = controlMapRevision;
    }

    void KbmGameplayFactProducer::ResetSyntheticSuppression(InputResetReasonMask) noexcept
    {
        _suppressionTokens.clear();
    }

    bool KbmGameplayFactProducer::ConsumeExactSyntheticReceipt(
        const KbmObservedEventDraft& event,
        std::uint32_t contextRevision,
        std::uint64_t ownerNowUs)
    {
        const auto match = std::find_if(
            _suppressionTokens.begin(),
            _suppressionTokens.end(),
            [&](const SyntheticKeyboardSuppressionToken& token) {
                return token.remainingMatches != 0 &&
                    token.provenanceMode != SyntheticProvenanceMode::Unproven &&
                    (token.expiresAtOwnerUs == 0 || ownerNowUs <= token.expiresAtOwnerUs) &&
                    event.physical.device == KbmPhysicalDevice::Keyboard &&
                    event.physical.idCode == token.scancode &&
                    event.phase == token.expectedPhase &&
                    contextRevision == token.contextRevision &&
                    event.syntheticToken == token.token &&
                    event.originatingOutputGeneration == token.originatingOutputGeneration &&
                    event.helperInjectionSequence == token.helperInjectionSequence;
            });
        if (match == _suppressionTokens.end()) {
            return false;
        }
        --match->remainingMatches;
        return true;
    }

    KbmGameplayCurrentFacts KbmGameplayFactProducer::BuildCurrentFacts(
        const KbmBindingSnapshot& bindings) const
    {
        KbmGameplayCurrentFacts current{};
        for (const auto& binding : bindings.entries) {
            if (!_physical.downCodes.Contains(binding.physical) ||
                _physical.quarantineCodes.Contains(binding.physical)) {
                continue;
            }
            const bool keyboard = binding.physical.device == KbmPhysicalDevice::Keyboard;
            switch (binding.gameplayClass) {
            case KbmGameplayClass::Move:
                if (keyboard) {
                    current.keyboardMoveHeldMask |= binding.semanticBit;
                }
                break;
            case KbmGameplayClass::Combat:
                if (keyboard) {
                    current.keyboardCombatHeldMask |= binding.semanticBit;
                } else {
                    current.mouseCombatHeldMask |= binding.semanticBit;
                }
                break;
            case KbmGameplayClass::TransientDigital:
                if (keyboard) {
                    current.keyboardTransientHeldMask |= binding.semanticBit;
                } else {
                    current.mouseTransientHeldMask |= binding.semanticBit;
                }
                break;
            case KbmGameplayClass::SustainedDigital:
                if (keyboard) {
                    current.keyboardSustainedHeldMask |= binding.semanticBit;
                } else {
                    current.mouseSustainedHeldMask |= binding.semanticBit;
                }
                break;
            case KbmGameplayClass::Look:
                break;
            }
        }
        return current;
    }

    void KbmGameplayFactProducer::AppendMappedEdges(
        KbmGameplayIngressBatchDraft& batch,
        const KbmObservedEventDraft& event,
        const KbmBindingSnapshot& bindings,
        KbmEdgeOrigin origin) const
    {
        for (const auto& binding : bindings.entries) {
            if (binding.physical != event.physical) {
                continue;
            }
            batch.orderedEdges.push_back(KbmGameplayEdgeDraft{
                .eventOrdinal = event.eventOrdinal,
                .producerTimestampUs = event.producerTimestampUs,
                .physical = event.physical,
                .gameplayClass = binding.gameplayClass,
                .actionId = binding.actionId,
                .phase = event.phase,
                .origin = origin,
                .deltaX = event.deltaX,
                .deltaY = event.deltaY
            });
        }
    }

    void KbmGameplayFactProducer::AppendSourceActivity(
        KbmGameplayIngressBatchDraft& batch,
        const KbmObservedEventDraft& event) const
    {
        SourceActivityKind kind = SourceActivityKind::KeyboardPress;
        if (event.phase == KbmEdgePhase::MouseDelta) {
            kind = SourceActivityKind::MouseDelta;
        } else if (event.physical.device == KbmPhysicalDevice::Mouse) {
            kind = SourceActivityKind::MouseButtonPress;
        }
        batch.sourceActivities.push_back(MeaningfulSourceActivityDraft{
            .source = event.physical.device == KbmPhysicalDevice::Keyboard ?
                PhysicalInputSource::Keyboard : PhysicalInputSource::Mouse,
            .kind = kind,
            .controlCode = event.physical.idCode,
            .deltaX = event.deltaX,
            .deltaY = event.deltaY,
            .producerTimestampUs = event.producerTimestampUs
        });
    }

    void KbmGameplayFactProducer::ApplyTrustedRawState(
        KbmGameplayIngressBatchDraft& batch,
        const KbmRawCurrentState& raw,
        const KbmBindingSnapshot& bindings)
    {
        const auto previousDown = _physical.downCodes;
        for (std::size_t index = 0; index < previousDown.count; ++index) {
            const auto code = previousDown.values[index];
            if (raw.downCodes.Contains(code) || _physical.quarantineCodes.Contains(code)) {
                continue;
            }
            (void)_physical.downCodes.Erase(code);
            const KbmObservedEventDraft reconciled{
                .producerTimestampUs = raw.providerGeneration,
                .physical = code,
                .phase = KbmEdgePhase::ReconciledRelease,
                .origin = KbmEdgeOrigin::Reconciled
            };
            AppendMappedEdges(batch, reconciled, bindings, KbmEdgeOrigin::Reconciled);
        }
        for (std::size_t index = 0; index < raw.downCodes.count; ++index) {
            const auto code = raw.downCodes.values[index];
            if (!_physical.quarantineCodes.Contains(code)) {
                (void)_physical.downCodes.Insert(code);
            }
        }
    }
}
