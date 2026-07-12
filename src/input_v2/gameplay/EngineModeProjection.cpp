#include "pch.h"

#include "input_v2/gameplay/EngineModeProjection.h"

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        constexpr std::size_t DomainIndex(EngineQueryDomain domain) noexcept
        {
            return static_cast<std::size_t>(domain);
        }
    }

    EngineModeDecisionSnapshot ProjectOriginalEngineModes(
        std::uint64_t ownerTickToken,
        std::uint64_t inputStateEpoch,
        std::uint32_t contextRevision,
        std::uint64_t runtimeGeneration) noexcept
    {
        EngineModeDecisionSnapshot snapshot{ .generation = runtimeGeneration };
        for (std::size_t index = 0; index < snapshot.byDomain.size(); ++index) {
            snapshot.byDomain[index] = EngineModeDecision{
                .domain = static_cast<EngineQueryDomain>(index),
                .mode = EngineInputMode::Original,
                .causality = EngineDecisionCausality::OriginalOnly,
                .ownerTickToken = ownerTickToken,
                .inputStateEpoch = inputStateEpoch,
                .contextRevision = contextRevision,
                .runtimeGeneration = runtimeGeneration
            };
        }
        return snapshot;
    }

    EngineModeDecisionSnapshot ProjectEngineModeShadow(
        const EngineModeProjectionInput& input) noexcept
    {
        auto snapshot = ProjectOriginalEngineModes(
            input.ownerTickToken,
            input.inputStateEpoch,
            input.contextRevision,
            input.runtimeGeneration);
        for (const auto& recommendation : input.recommendations) {
            const auto index = DomainIndex(recommendation.domain);
            if (index >= snapshot.byDomain.size()) {
                continue;
            }
            auto& decision = snapshot.byDomain[index];
            decision.mode = recommendation.mode;
            decision.causality = recommendation.causality;
        }
        return snapshot;
    }
}
