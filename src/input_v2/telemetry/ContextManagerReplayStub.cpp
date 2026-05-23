#include "pch.h"
#include "input/InputContext.h"

#include "input_v2/context/ContextRefreshTick.h"
#include "input_v2/context/ContextResolver.h"

namespace dualpad::input
{
    ContextManager& ContextManager::GetSingleton()
    {
        static ContextManager instance;
        return instance;
    }

    InputContext ContextManager::GetCurrentContext() const
    {
        std::scoped_lock lock(_mutex);
        return _currentContext;
    }

    std::uint32_t ContextManager::GetCurrentEpoch() const
    {
        std::scoped_lock lock(_mutex);
        return _contextEpoch;
    }

    void ContextManager::OnMenuOpen(std::string_view)
    {
    }

    void ContextManager::OnMenuClose(std::string_view)
    {
    }

    void ContextManager::ApplyResolvedContext(const dualpad::input_v2::context::LegacyContextMirrorState& state)
    {
        std::scoped_lock lock(_mutex);
        _currentContext = state.context;
        _contextEpoch = state.epoch;
        _baseContext = state.context;
    }

    void ContextManager::UpdateFrameState()
    {
    }

    void ContextManager::UpdateGameplayContext()
    {
        auto& refresh = dualpad::input_v2::context::ContextRefreshTick::GetSingleton();
        refresh.RefreshOnMainThread(refresh.BeginFrame());
    }

    void ContextManager::PushContext(InputContext context)
    {
        SetContext(context);
    }

    void ContextManager::PopContext()
    {
        SetContext(InputContext::Gameplay);
    }

    void ContextManager::SetContext(InputContext context)
    {
        std::scoped_lock lock(_mutex);
        if (_currentContext != context) {
            _currentContext = context;
            ++_contextEpoch;
        }
        _baseContext = context;
    }

    InputContext ContextManager::DetectGameplayContext() const
    {
        return InputContext::Gameplay;
    }

    void ContextManager::SetCurrentContextLocked(InputContext context)
    {
        if (_currentContext != context) {
            _currentContext = context;
            ++_contextEpoch;
        }
    }
}
