#pragma once
#include "input/Trigger.h"
#include "input/InputContext.h"
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace dualpad::input
{
    struct Binding
    {
        Trigger trigger;
        std::string actionId;
        InputContext context;
    };

    struct BindingSubsetDiagnostics
    {
        std::uint32_t requestedModifierMask{ 0 };
        std::size_t requestedModifierCount{ 0 };
        std::size_t equivalentBestCount{ 0 };
        std::vector<Binding> equivalentBestBindings;
    };

    // Stores context-sensitive bindings and serves both config and runtime lookups.
    class BindingManager
    {
    public:
        static BindingManager& GetSingleton();

        void AddBinding(const Binding& binding);
        void AddBindingIfMissing(const Binding& binding);
        void RemoveBinding(const Trigger& trigger, InputContext context);
        void ClearBindings();
        void MergeBindings(InputContext sourceContext, InputContext targetContext, bool overwriteExisting = false);

        std::optional<std::string> GetActionForTrigger(
            const Trigger& trigger,
            InputContext context) const;
        std::optional<Binding> FindBestBindingForTriggerSubset(
            const Trigger& trigger,
            InputContext context,
            bool allowEmptyModifiers,
            BindingSubsetDiagnostics* diagnostics = nullptr) const;

        std::optional<Trigger> GetTriggerForAction(
            std::string_view actionId,
            InputContext context) const;
        std::uint32_t GetComboParticipantMask(InputContext context) const;
        bool HasConfiguredComboPair(InputContext context, std::uint32_t firstButton, std::uint32_t secondButton) const;

        // Seeds a fallback set when no external config is present.
        void InitDefaultBindings();
        std::size_t ApplyStandardFallbackBindings();

    private:
        BindingManager() = default;

        mutable std::mutex _mutex;
        std::unordered_map<InputContext, std::uint32_t> _comboParticipantMasks;
        std::unordered_map<InputContext, std::unordered_set<std::uint64_t>> _comboPairsByContext;

        std::unordered_map<
            InputContext,
            std::unordered_map<Trigger, std::string, TriggerHash>
        > _bindings;

        void RebuildComboCachesLocked();
        static std::uint64_t MakeComboPairKey(std::uint32_t firstButton, std::uint32_t secondButton);
    };
}
