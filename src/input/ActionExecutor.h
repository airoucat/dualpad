#pragma once
#include "input/InputActions.h"

#include <string_view>

namespace dualpad::input
{
    class IActionExecutor
    {
    public:
        virtual ~IActionExecutor() = default;

        // true = handled
        virtual bool ExecuteButton(std::string_view actionId, TriggerPhase phase) = 0;
        virtual bool ExecuteAxis(std::string_view actionId, float value) = 0;
    };

    // 全局组合执行器（顺序：Direct -> Native）
    IActionExecutor& GetCompositeGameExecutor();
}