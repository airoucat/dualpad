#include "pch.h"
#include "input/mapping/BindingResolver.h"

#include "input/BindingManager.h"
#include "input/mapping/TriggerMapper.h"

namespace dualpad::input
{
    namespace
    {
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
            if (trigger->type == TriggerType::Button) {
                auto comboTrigger = *trigger;
                comboTrigger.type = TriggerType::Combo;
                if (auto resolved = TryResolve(comboTrigger, context)) {
                    return resolved;
                }
            }

            auto fallbackTrigger = *trigger;
            fallbackTrigger.modifiers.clear();
            if (auto resolved = TryResolve(fallbackTrigger, context)) {
                return resolved;
            }
        }

        return std::nullopt;
    }
}
