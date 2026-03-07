#pragma once
#include "input/Trigger.h"
#include "input/InputContext.h"
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>

namespace dualpad::input
{
    struct Binding
    {
        Trigger trigger;
        std::string actionId;
        InputContext context;
    };

    // Stores context-sensitive bindings and serves both config and runtime lookups.
    class BindingManager
    {
    public:
        static BindingManager& GetSingleton();

        void AddBinding(const Binding& binding);
        void RemoveBinding(const Trigger& trigger, InputContext context);
        void ClearBindings();
        void MergeBindings(InputContext sourceContext, InputContext targetContext, bool overwriteExisting = false);

        std::optional<std::string> GetActionForTrigger(
            const Trigger& trigger,
            InputContext context) const;

        std::optional<Trigger> GetTriggerForAction(
            std::string_view actionId,
            InputContext context) const;

        // Seeds a fallback set when no external config is present.
        void InitDefaultBindings();

    private:
        BindingManager() = default;

        mutable std::mutex _mutex;

        std::unordered_map<
            InputContext,
            std::unordered_map<Trigger, std::string, TriggerHash>
        > _bindings;
    };
}
