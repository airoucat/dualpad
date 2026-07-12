#pragma once

#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/ingress/InputResetReason.h"
#include "input_v2/ingress/MeaningfulSourceActivity.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dualpad::input_v2::ingress
{
    enum class KbmPhysicalDevice : std::uint8_t
    {
        Keyboard = 0,
        Mouse
    };

    inline constexpr std::uint32_t kMouseDeltaPhysicalIdCode = 0xFFFFFFFFu;

    struct KbmPhysicalCode
    {
        KbmPhysicalDevice device{ KbmPhysicalDevice::Keyboard };
        std::uint32_t idCode{ 0 };

        friend bool operator==(const KbmPhysicalCode&, const KbmPhysicalCode&) = default;
    };

    inline constexpr std::size_t kMaxKbmPhysicalCodes = 128;

    struct KbmPhysicalCodeSet
    {
        std::array<KbmPhysicalCode, kMaxKbmPhysicalCodes> values{};
        std::size_t count{ 0 };

        [[nodiscard]] bool Contains(KbmPhysicalCode code) const noexcept
        {
            return std::find(values.begin(), values.begin() + count, code) != values.begin() + count;
        }

        bool Insert(KbmPhysicalCode code) noexcept
        {
            if (Contains(code)) {
                return true;
            }
            if (count == values.size()) {
                return false;
            }
            const auto less = [](const KbmPhysicalCode& left, const KbmPhysicalCode& right) {
                if (left.device != right.device) {
                    return left.device < right.device;
                }
                return left.idCode < right.idCode;
            };
            const auto position = std::lower_bound(values.begin(), values.begin() + count, code, less);
            std::move_backward(position, values.begin() + count, values.begin() + count + 1);
            *position = code;
            ++count;
            return true;
        }

        bool Erase(KbmPhysicalCode code) noexcept
        {
            const auto position = std::find(values.begin(), values.begin() + count, code);
            if (position == values.begin() + count) {
                return false;
            }
            std::move(position + 1, values.begin() + count, position);
            --count;
            return true;
        }
    };

    enum class KbmGameplayClass : std::uint8_t
    {
        Look = 0,
        Move,
        Combat,
        TransientDigital,
        SustainedDigital
    };

    enum class KbmEdgePhase : std::uint8_t
    {
        Press = 0,
        Release,
        MouseDelta,
        ReconciledRelease
    };

    enum class KbmEdgeOrigin : std::uint8_t
    {
        Physical = 0,
        SyntheticSuppressed,
        Reconciled
    };

    enum class KbmBaselineState : std::uint8_t
    {
        Clean = 0,
        AwaitPhysicalRearm,
        MappingRearmRequired,
        ProviderIncomplete
    };

    struct KbmBindingEntry
    {
        KbmPhysicalCode physical{};
        actions::ActionId actionId{};
        KbmGameplayClass gameplayClass{ KbmGameplayClass::TransientDigital };
        std::uint32_t semanticBit{ 0 };
    };

    struct KbmBindingSnapshot
    {
        std::uint64_t generation{ 0 };
        std::uint64_t controlMapFingerprint{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        std::uint32_t contextRevision{ 0 };
        bool complete{ true };
        std::vector<KbmBindingEntry> entries;
    };

    struct KbmRawCurrentState
    {
        std::uint64_t providerGeneration{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        KbmPhysicalCodeSet downCodes{};
        bool physicalOnlyProvenance{ false };
        bool complete{ false };
    };

    class IKbmCurrentStateProvider
    {
    public:
        virtual ~IKbmCurrentStateProvider() = default;
        virtual KbmRawCurrentState ReadComplete(const KbmBindingSnapshot& bindings) const noexcept = 0;
    };

    struct KbmObservedEventDraft
    {
        std::uint32_t eventOrdinal{ 0 };
        std::uint64_t producerTimestampUs{ 0 };
        KbmPhysicalCode physical{};
        KbmEdgePhase phase{ KbmEdgePhase::Press };
        KbmEdgeOrigin origin{ KbmEdgeOrigin::Physical };
        std::int32_t deltaX{ 0 };
        std::int32_t deltaY{ 0 };
        bool initialPress{ false };
        std::uint64_t syntheticToken{ 0 };
        std::uint64_t originatingOutputGeneration{ 0 };
        std::uint64_t helperInjectionSequence{ 0 };
    };

    struct KbmGameplayEdgeDraft
    {
        std::uint32_t eventOrdinal{ 0 };
        std::uint64_t producerTimestampUs{ 0 };
        KbmPhysicalCode physical{};
        KbmGameplayClass gameplayClass{ KbmGameplayClass::TransientDigital };
        actions::ActionId actionId{};
        KbmEdgePhase phase{ KbmEdgePhase::Press };
        KbmEdgeOrigin origin{ KbmEdgeOrigin::Physical };
        std::int32_t deltaX{ 0 };
        std::int32_t deltaY{ 0 };
    };

    struct KbmGameplayEdge : KbmGameplayEdgeDraft
    {
        std::uint64_t ingressSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    struct KbmGameplayCurrentFacts
    {
        std::uint32_t keyboardMoveHeldMask{ 0 };
        std::uint32_t keyboardCombatHeldMask{ 0 };
        std::uint32_t mouseCombatHeldMask{ 0 };
        std::uint32_t keyboardTransientHeldMask{ 0 };
        std::uint32_t mouseTransientHeldMask{ 0 };
        std::uint32_t keyboardSustainedHeldMask{ 0 };
        std::uint32_t mouseSustainedHeldMask{ 0 };
        bool complete{ false };
    };

    struct KbmPhysicalLedger
    {
        KbmPhysicalCodeSet downCodes{};
        KbmPhysicalCodeSet quarantineCodes{};
        std::uint64_t physicalEpoch{ 0 };
        bool complete{ false };

        [[nodiscard]] bool QuarantineDrained() const noexcept { return quarantineCodes.count == 0; }
    };

    struct LatestKbmGameplayFacts
    {
        CausalLatestHeader causal{};
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        std::uint64_t bindingGeneration{ 0 };
        KbmGameplayCurrentFacts current{};
        KbmPhysicalLedger physical{};
        std::uint64_t keyboardSustainedEventOrdinal{ 0 };
        std::uint64_t mouseSustainedEventOrdinal{ 0 };
        std::uint64_t lastPhysicalMouseMoveOwnerUs{ 0 };
        bool physicalMouseMoveThisFrame{ false };
        KbmBaselineState baseline{ KbmBaselineState::Clean };
        InputResetReasonMask resetReasons{ 0 };
        bool virtualGameplayEligible{ true };
    };

    struct KbmGameplayIngressBatchDraft
    {
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        std::uint64_t bindingGeneration{ 0 };
        std::uint64_t controlMapFingerprint{ 0 };
        std::vector<KbmGameplayEdgeDraft> orderedEdges;
        std::vector<MeaningfulSourceActivityDraft> sourceActivities;
        KbmGameplayCurrentFacts completeCurrent{};
        KbmPhysicalLedger physical{};
        std::uint64_t lastPhysicalMouseMoveOwnerUs{ 0 };
        KbmBaselineState baseline{ KbmBaselineState::Clean };
    };

    struct KbmObservedBatch
    {
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        std::vector<KbmObservedEventDraft> events;
        KbmRawCurrentState rawCurrent{};
        bool eventListComplete{ false };
    };

    enum class SyntheticProvenanceMode : std::uint8_t
    {
        Unproven = 0,
        VerifiedPhysicalOnlyProvider,
        ReservedNonCollidingControl
    };

    struct SyntheticKeyboardSuppressionToken
    {
        std::uint64_t token{ 0 };
        std::uint8_t scancode{ 0 };
        KbmEdgePhase expectedPhase{ KbmEdgePhase::Press };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t originatingOutputGeneration{ 0 };
        std::uint64_t helperInjectionSequence{ 0 };
        SyntheticProvenanceMode provenanceMode{ SyntheticProvenanceMode::Unproven };
        std::uint8_t remainingMatches{ 0 };
        std::uint64_t expiresAtOwnerUs{ 0 };
    };
}
