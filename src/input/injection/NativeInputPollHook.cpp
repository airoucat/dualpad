#include "pch.h"
#include "input/injection/NativeInputPollHook.h"

#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/PadEventSnapshotProcessor.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        using PollInputDevices_t = void(RE::BSInputDeviceManager*, float);

        struct PollInputDevicesHook
        {
            static void Thunk(RE::BSInputDeviceManager* inputManager, float secsSinceLastFrame)
            {
                auto& config = RuntimeConfig::GetSingleton();
                if (config.UseNativeButtonInjector()) {
                    PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread();
                    (void)PadEventSnapshotProcessor::GetSingleton().FlushInjectedInputQueue();
                }

                _original(inputManager, secsSinceLastFrame);
            }

            static inline REL::Relocation<PollInputDevices_t> _original;
        };
    }

    NativeInputPollHook& NativeInputPollHook::GetSingleton()
    {
        static NativeInputPollHook instance;
        return instance;
    }

    void NativeInputPollHook::Install()
    {
        if (_installed) {
            return;
        }

        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(67315, 68617) };
        PollInputDevicesHook::_original = target.write_branch<5>(PollInputDevicesHook::Thunk);
        _installed = PollInputDevicesHook::_original.address() != 0;

        logger::info(
            "[DualPad][NativePoll] PollInputDevices hook installed={} se_id={} ae_id={}",
            _installed,
            67315,
            68617);
    }

    bool NativeInputPollHook::IsInstalled() const
    {
        return _installed;
    }
}
