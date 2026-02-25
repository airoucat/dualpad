#pragma once
#include "input/InputActions.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace dualpad::input
{
    struct ActionEvent
    {
        std::string actionId;  // e.g. Input.Square.Press / Game.OpenInventory
        TriggerCode code{ TriggerCode::None };
        TriggerPhase phase{ TriggerPhase::Press };
        std::chrono::steady_clock::time_point when{};
    };

    class ActionRouter
    {
    public:
        static ActionRouter& GetSingleton();

        void InitDefaultBindings();

        // 所有输入都走这里
        void EmitInput(TriggerCode code, TriggerPhase phase);

        // 主线程后续消费
        std::vector<ActionEvent> Drain();

        struct BindingEntry
        {
            TriggerCode code{ TriggerCode::None };
            TriggerPhase phase{ TriggerPhase::Press };
            std::string actionId;
        };
        // 用新绑定整体替换（线程安全）
        void ReplaceBindings(std::vector<BindingEntry> entries);

    private:
        ActionRouter() = default;

        struct TriggerKey {
            TriggerCode code;
            TriggerPhase phase;
            bool operator==(const TriggerKey& r) const noexcept
            {
                return code == r.code && phase == r.phase;
            }
        };
        struct TriggerKeyHash {
            std::size_t operator()(const TriggerKey& k) const noexcept
            {
                return (static_cast<std::size_t>(k.code) << 8) ^ static_cast<std::size_t>(k.phase);
            }
        };

        std::string BuildInputActionId(TriggerCode code, TriggerPhase phase) const;

        std::mutex _mtx;
        std::vector<ActionEvent> _pending;

        // Trigger -> 自定义 Action（最小绑定）
        std::unordered_map<TriggerKey, std::string, TriggerKeyHash> _bindings;
    };
}