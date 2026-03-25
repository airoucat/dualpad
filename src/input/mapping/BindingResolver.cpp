#include "pch.h"
#include "input/mapping/BindingResolver.h"

#include <SKSE/SKSE.h>
#include <format>

#include "input/BindingManager.h"
#include "input/RuntimeConfig.h"
#include "input/mapping/TriggerMapper.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr bool kAllowFallbackWithoutModifiers = true;

        std::uint32_t ModifiersToMask(const std::vector<std::uint32_t>& modifiers)
        {
            std::uint32_t mask = 0;
            for (const auto modifier : modifiers) {
                mask |= modifier;
            }
            return mask;
        }

        std::string DescribeTrigger(const Trigger& trigger)
        {
            return std::format(
                "{}:code=0x{:08X},mods=0x{:08X}",
                ToString(trigger.type),
                trigger.code,
                ModifiersToMask(trigger.modifiers));
        }

        void LogExactResolve(const Trigger& trigger, InputContext context, std::string_view actionId)
        {
            if (!RuntimeConfig::GetSingleton().LogMappingEvents()) {
                return;
            }

            if (trigger.modifiers.empty() &&
                trigger.type != TriggerType::Combo &&
                trigger.type != TriggerType::Layer) {
                return;
            }

            logger::debug(
                "[DualPad][BindingResolver] exact resolve context={} trigger={} action={}",
                ToString(context),
                DescribeTrigger(trigger),
                actionId);
        }

        void LogSubsetResolve(
            const Trigger& requestedTrigger,
            InputContext context,
            const BindingSubsetDiagnostics& diagnostics,
            const Binding& resolvedBinding)
        {
            if (!RuntimeConfig::GetSingleton().LogMappingEvents()) {
                return;
            }

            std::string candidateSummary;
            if (diagnostics.equivalentBestCount > 1) {
                bool first = true;
                for (const auto& candidate : diagnostics.equivalentBestBindings) {
                    if (!first) {
                        candidateSummary += "; ";
                    }
                    first = false;
                    candidateSummary += std::format(
                        "{}->{}",
                        DescribeTrigger(candidate.trigger),
                        candidate.actionId);
                }
            }

            std::string suffix;
            if (!candidateSummary.empty()) {
                suffix = std::format(" candidates=[{}]", candidateSummary);
            }

            logger::debug(
                "[DualPad][BindingResolver] subset fallback context={} requested={} resolved={} action={} allowEmptyModifiers={} ambiguousCandidates={}{}",
                ToString(context),
                DescribeTrigger(requestedTrigger),
                DescribeTrigger(resolvedBinding.trigger),
                resolvedBinding.actionId,
                kAllowFallbackWithoutModifiers,
                diagnostics.equivalentBestCount,
                suffix);
        }

        std::optional<ResolvedBinding> TryResolve(const Trigger& trigger, InputContext context)
        {
            auto actionId = BindingManager::GetSingleton().GetActionForTrigger(trigger, context);
            if (!actionId) {
                return std::nullopt;
            }

            LogExactResolve(trigger, context, *actionId);

            ResolvedBinding resolved{};
            resolved.trigger = trigger;
            resolved.actionId = std::move(*actionId);
            return resolved;
        }

        std::optional<ResolvedBinding> TryResolveSubset(
            const Trigger& trigger,
            InputContext context,
            bool allowEmptyModifiers)
        {
            BindingSubsetDiagnostics diagnostics;
            auto binding = BindingManager::GetSingleton().FindBestBindingForTriggerSubset(
                trigger,
                context,
                allowEmptyModifiers,
                &diagnostics);
            if (!binding) {
                return std::nullopt;
            }

            LogSubsetResolve(trigger, context, diagnostics, *binding);

            ResolvedBinding resolved{};
            resolved.trigger = binding->trigger;
            resolved.actionId = std::move(binding->actionId);
            return resolved;
        }
    }

    std::optional<ResolvedBinding> BindingResolver::Resolve(const PadEvent& event, InputContext context) const
    {
        auto trigger = TriggerMapper::TryMapEvent(event);
        if (!trigger) {
            return std::nullopt;
        }

        if (auto resolved = TryResolve(*trigger, context)) {
            return resolved;
        }

        if (trigger->type != TriggerType::Combo &&
            !trigger->modifiers.empty()) {
            if (auto resolved = TryResolveSubset(*trigger, context, kAllowFallbackWithoutModifiers)) {
                return resolved;
            }
        }

        return std::nullopt;
    }
}
