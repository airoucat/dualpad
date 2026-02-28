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

    class BindingManager
    {
    public:
        static BindingManager& GetSingleton();

        void AddBinding(const Binding& binding);
        void RemoveBinding(const Trigger& trigger, InputContext context);

        std::optional<std::string> GetActionForTrigger(
            const Trigger& trigger,
            InputContext context) const;

        std::optional<Trigger> GetTriggerForAction(
            std::string_view actionId,
            InputContext context) const;

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