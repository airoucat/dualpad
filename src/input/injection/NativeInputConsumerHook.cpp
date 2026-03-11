#include "pch.h"
#include "input/injection/NativeInputConsumerHook.h"

#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotProcessor.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        using InputEventSource = RE::BSTEventSource<RE::InputEvent*>;
        using ProcessEventResult = RE::BSEventNotifyControl;
        using PlayerProcessEvent_t = ProcessEventResult(RE::PlayerControls*, RE::InputEvent* const*, InputEventSource*);
        using MenuProcessEvent_t = ProcessEventResult(RE::MenuControls*, RE::InputEvent* const*, InputEventSource*);

        template <class TControls, class TOriginal>
        ProcessEventResult InvokeWithInjectedEvents(
            TControls* controls,
            RE::InputEvent* const* event,
            InputEventSource* eventSource,
            TOriginal& original,
            std::string_view consumerName)
        {
            auto& config = RuntimeConfig::GetSingleton();
            if (!config.UseNativeButtonInjector()) {
                return original(controls, event, eventSource);
            }

            auto& processor = PadEventSnapshotProcessor::GetSingleton();
            RE::InputEvent* combinedHead = event ? *event : nullptr;
            processor.PrependInjectedInputEvents(combinedHead);

            const bool injected = combinedHead != (event ? *event : nullptr);
            const auto result = original(controls, injected ? &combinedHead : event, eventSource);
            processor.ReleaseInjectedInputEvents();

            if (injected && config.LogNativeInjection()) {
                logger::info("[DualPad][NativeHook] Submitted staged native events through {}", consumerName);
            }

            return result;
        }

        struct PlayerControlsHook
        {
            static ProcessEventResult Thunk(
                RE::PlayerControls* controls,
                RE::InputEvent* const* event,
                InputEventSource* eventSource)
            {
                return InvokeWithInjectedEvents(
                    controls,
                    event,
                    eventSource,
                    _original,
                    "PlayerControls::ProcessEvent");
            }

            static inline REL::Relocation<PlayerProcessEvent_t> _original;
        };

        struct MenuControlsHook
        {
            static ProcessEventResult Thunk(
                RE::MenuControls* controls,
                RE::InputEvent* const* event,
                InputEventSource* eventSource)
            {
                return InvokeWithInjectedEvents(
                    controls,
                    event,
                    eventSource,
                    _original,
                    "MenuControls::ProcessEvent");
            }

            static inline REL::Relocation<MenuProcessEvent_t> _original;
        };
    }

    NativeInputConsumerHook& NativeInputConsumerHook::GetSingleton()
    {
        static NativeInputConsumerHook instance;
        return instance;
    }

    void NativeInputConsumerHook::Install()
    {
        if (_playerControlsInstalled || _menuControlsInstalled) {
            return;
        }

        if (RuntimeConfig::GetSingleton().UseNativeButtonInjector()) {
            logger::info("[DualPad][NativeHook] Installing native button consumer hooks");
        }

        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerControls[0] };
        PlayerControlsHook::_original = playerVtbl.write_vfunc(0x1, PlayerControlsHook::Thunk);
        _playerControlsInstalled = PlayerControlsHook::_original.address() != 0;

        REL::Relocation<std::uintptr_t> menuVtbl{ RE::VTABLE_MenuControls[0] };
        MenuControlsHook::_original = menuVtbl.write_vfunc(0x1, MenuControlsHook::Thunk);
        _menuControlsInstalled = MenuControlsHook::_original.address() != 0;

        logger::info(
            "[DualPad][NativeHook] Consumer hooks installed player={} menu={}",
            _playerControlsInstalled,
            _menuControlsInstalled);
    }

    bool NativeInputConsumerHook::IsInstalled() const
    {
        return _playerControlsInstalled || _menuControlsInstalled;
    }
}
