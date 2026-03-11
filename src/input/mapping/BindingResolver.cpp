#include "pch.h"
#include "input/mapping/BindingResolver.h"

#include "input/BindingManager.h"
#include "input/mapping/TriggerMapper.h"

namespace dualpad::input
{
    namespace
    {
        constexpr bool kAllowFallbackWithoutModifiers = true;

        std::optional<ResolvedBinding> TryResolve(const Trigger& trigger, InputContext context)
        {
            auto actionId = BindingManager::GetSingleton().GetActionForTrigger(trigger, context);
            if (!actionId) {
                return std::nullopt;
            }

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
            auto binding = BindingManager::GetSingleton().FindBestBindingForTriggerSubset(
                trigger,
                context,
                allowEmptyModifiers);
            if (!binding) {
                return std::nullopt;
            }

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

        if (!trigger->modifiers.empty()) {
            if (auto resolved = TryResolveSubset(*trigger, context, kAllowFallbackWithoutModifiers)) {
                return resolved;
            }
        }

        return std::nullopt;
    }
}
