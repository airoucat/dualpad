#include "pch.h"
#include "input/NativeUserEventBridge.h"

namespace dualpad::input
{
    NativeUserEventBridge& NativeUserEventBridge::GetSingleton()
    {
        static NativeUserEventBridge s;
        return s;
    }

    void NativeUserEventBridge::SetSubmitter(Submitter fn)
    {
        std::scoped_lock lk(_mtx);
        _submitter = std::move(fn);
    }

    bool NativeUserEventBridge::HasSubmitter() const
    {
        std::scoped_lock lk(_mtx);
        return static_cast<bool>(_submitter);
    }

    void NativeUserEventBridge::Enqueue(TriggerCode code, TriggerPhase phase)
    {
        if (code == TriggerCode::None) {
            return;
        }
        std::scoped_lock lk(_mtx);
        _queue.push_back(NativePadEvent{ code, phase });
    }

    void NativeUserEventBridge::FlushQueued()
    {
        std::vector<NativePadEvent> local;
        Submitter submitter;

        {
            std::scoped_lock lk(_mtx);
            local.swap(_queue);
            submitter = _submitter;
        }

        if (!submitter) {
            return;
        }

        for (auto& e : local) {
            submitter(e);
        }
    }
}