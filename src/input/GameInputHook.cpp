#include "pch.h"
#include "input/GameInputHook.h"
#include "input/GameActionOutput.h"
#include "input/NativeUserEventBridge.h"

#include <SKSE/SKSE.h>
#include <mutex>

namespace logger = SKSE::log;

namespace
{
    using dualpad::input::NativePadEvent;
    using dualpad::input::TriggerCode;
    using dualpad::input::TriggerPhase;

    std::once_flag g_hookOnce;
    std::once_flag g_submitterOnce;
    std::once_flag g_dispatchWarnOnce;
    std::once_flag g_mapWarnOnce;

    // -----------------------------
    // Native submitter helpers
    // -----------------------------
    bool MapToGamepadButtonID(TriggerCode code, std::uint32_t& outKey)
    {
        switch (code) {
        case TriggerCode::Cross:     outKey = 0;  return true; // A
        case TriggerCode::Circle:    outKey = 1;  return true; // B
        case TriggerCode::Square:    outKey = 2;  return true; // X
        case TriggerCode::Triangle:  outKey = 3;  return true; // Y
        case TriggerCode::L1:        outKey = 4;  return true;
        case TriggerCode::R1:        outKey = 5;  return true;
        case TriggerCode::Create:    outKey = 8;  return true; // Back
        case TriggerCode::Options:   outKey = 9;  return true; // Start
        case TriggerCode::L3:        outKey = 10; return true;
        case TriggerCode::R3:        outKey = 11; return true;
        case TriggerCode::DpadUp:    outKey = 12; return true;
        case TriggerCode::DpadDown:  outKey = 13; return true;
        case TriggerCode::DpadLeft:  outKey = 14; return true;
        case TriggerCode::DpadRight: outKey = 15; return true;
        default:
            return false;
        }
    }

    const char* FallbackUserEvent(std::uint32_t gamepadBtnID)
    {
        switch (gamepadBtnID) {
        case 0:  return "Accept";
        case 1:  return "Cancel";
        case 2:  return "XButton";
        case 3:  return "YButton";
        case 4:  return "LB";
        case 5:  return "RB";
        case 8:  return "Back";
        case 9:  return "Start";
        case 10: return "LThumb";
        case 11: return "RThumb";
        case 12: return "Up";
        case 13: return "Down";
        case 14: return "Left";
        case 15: return "Right";
        default: return "";
        }
    }

    RE::ButtonEvent* MakeButtonEvent(std::uint32_t gamepadBtnID, bool down)
    {
        auto* mgr = RE::BSInputDeviceManager::GetSingleton();
        if (!mgr) {
            return nullptr;
        }

        std::uint32_t keyCode = 0;
        if (!mgr->GetDeviceMappedKeycode(RE::INPUT_DEVICE::kGamepad, gamepadBtnID, keyCode)) {
            std::call_once(g_mapWarnOnce, []() {
                logger::warn("[DualPad] GetDeviceMappedKeycode failed, using fallback keycode offset");
                });
            keyCode = 266 + gamepadBtnID;
        }

        RE::BSFixedString userEvent;
        if (!mgr->GetDeviceKeyMapping(RE::INPUT_DEVICE::kGamepad, gamepadBtnID, userEvent)) {
            userEvent = RE::BSFixedString(FallbackUserEvent(gamepadBtnID));
        }

        const float value = down ? 1.0f : 0.0f;
        const float held = down ? 0.0f : 0.016f;

        return RE::ButtonEvent::Create(
            RE::INPUT_DEVICE::kGamepad,
            userEvent,
            keyCode,
            value,
            held);
    }

    bool DispatchInputEvent(RE::InputEvent* ev)
    {
        auto* mgr = RE::BSInputDeviceManager::GetSingleton();
        if (!mgr || !ev) {
            return false;
        }

        RE::InputEvent* head = ev;
        mgr->SendEvent(&head);
        return true;
    }

    void SubmitToGame(const NativePadEvent& e)
    {
        std::uint32_t btnID = 0;
        if (!MapToGamepadButtonID(e.code, btnID)) {
            return;
        }

        auto sendOne = [&](bool down) {
            auto* btn = MakeButtonEvent(btnID, down);
            if (!btn) {
                return;
            }

            RE::InputEvent* base = btn;
            const bool ok = DispatchInputEvent(base);
            if (!ok) {
                std::call_once(g_dispatchWarnOnce, []() {
                    logger::warn("[DualPad] Native dispatch failed (event not delivered)");
                    });
            }
            };

        switch (e.phase) {
        case TriggerPhase::Press:
            sendOne(true);
            break;
        case TriggerPhase::Release:
            sendOne(false);
            break;
        case TriggerPhase::Pulse:
            sendOne(true);
            sendOne(false);
            break;
        }
    }

    // -----------------------------
    // PlayerControls::ProcessEvent hook
    // -----------------------------
    class PlayerControlsProcessEventHook
    {
    public:
        static void Install()
        {
            REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerControls[0] };
            _orig = vtbl.write_vfunc(0x1, Thunk);
        }

    private:
        static RE::BSEventNotifyControl Thunk(
            RE::PlayerControls* a_this,
            RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_source)
        {
            const auto ret = _orig(a_this, a_event, a_source);

            // 原函数后执行，避免被后续输入流程覆盖
            static thread_local bool s_inFlush = false;
            if (!s_inFlush) {
                s_inFlush = true;
                dualpad::input::CollectGameOutputCommands();
                dualpad::input::FlushGameOutputOnBoundThread();
                dualpad::input::NativeUserEventBridge::GetSingleton().FlushQueued();
                s_inFlush = false;
            }

            return ret;
        }

        static inline REL::Relocation<decltype(Thunk)> _orig;
    };
}

namespace dualpad::input
{
    void InstallGameInputHook()
    {
        std::call_once(g_hookOnce, []() {
            PlayerControlsProcessEventHook::Install();
            logger::info("[DualPad] InstallGameInputHook: PlayerControls::ProcessEvent hooked");
            });
    }

    void InstallNativeSubmitter()
    {
        std::call_once(g_submitterOnce, []() {
            auto& bridge = NativeUserEventBridge::GetSingleton();
            bridge.SetSubmitter([](const NativePadEvent& e) {
                SubmitToGame(e);
                });
            logger::info("[DualPad] InstallNativeSubmitter: installed");
            });
    }
}