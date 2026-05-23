#include "pch.h"
#include "input/ActionExecutor.h"

namespace dualpad::input
{
    ActionExecutor& ActionExecutor::GetSingleton()
    {
        static ActionExecutor instance;
        return instance;
    }

    bool ActionExecutor::Execute(std::string_view, InputContext)
    {
        return false;
    }
}
