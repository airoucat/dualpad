#include "pch.h"
#include "input/backend/ButtonEventBackend.h"

namespace dualpad::input::backend
{
    ButtonEventBackend& ButtonEventBackend::GetSingleton()
    {
        static ButtonEventBackend instance;
        return instance;
    }

    void ButtonEventBackend::Reset()
    {
    }

    bool ButtonEventBackend::IsRouteActive() const
    {
        return false;
    }

    bool ButtonEventBackend::CanHandleAction(std::string_view) const
    {
        return false;
    }

    bool ButtonEventBackend::TriggerAction(
        std::string_view,
        ActionOutputContract,
        InputContext)
    {
        return false;
    }

    bool ButtonEventBackend::SubmitActionState(
        std::string_view,
        ActionOutputContract,
        bool,
        float,
        InputContext)
    {
        return false;
    }
}
