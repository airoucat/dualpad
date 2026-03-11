#pragma once
#include "input/InputContext.h"
#include <string_view>

namespace dualpad::input
{
    class ActionExecutor
    {
    public:
        static ActionExecutor& GetSingleton();

        bool Execute(std::string_view actionId, InputContext context);

    private:
        ActionExecutor() = default;

        bool ExecutePluginAction(std::string_view actionId);
    };
}
