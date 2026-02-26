#include "pch.h"
#include "input/GameInputHook.h"
#include "input/GameActionOutput.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        struct PlayerControlsHook
        {
            using Fn = RE::BSEventNotifyControl(
                RE::PlayerControls*,
                RE::InputEvent* const*,
                RE::BSTEventSource<RE::InputEvent*>*);

            static RE::BSEventNotifyControl Thunk(
                RE::PlayerControls* self,
                RE::InputEvent* const* events,
                RE::BSTEventSource<RE::InputEvent*>* source)
            {
                // 先走原版输入逻辑
                auto ret = _orig(self, events, source);

                // 再覆盖写入（关键）
                if (self == RE::PlayerControls::GetSingleton()) {
                    CollectGameOutputCommands();
                    FlushGameOutputOnBoundThread();
                }

                return ret;
            }

            static inline REL::Relocation<Fn> _orig;
        };
    }

    void InstallGameInputHook()
    {
        // 常见写法：PlayerControls vtable 第0组，ProcessEvent通常在 0x1
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerControls[0] };
        PlayerControlsHook::_orig = vtbl.write_vfunc(0x1, &PlayerControlsHook::Thunk);

        logger::info("[DualPad] PlayerControls::ProcessEvent hook installed");
    }
}