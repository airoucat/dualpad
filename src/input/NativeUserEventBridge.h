#pragma once
#include "input/InputActions.h"
#include <functional>
#include <mutex>
#include <vector>

namespace dualpad::input
{
    struct NativePadEvent
    {
        TriggerCode code{ TriggerCode::None };
        TriggerPhase phase{ TriggerPhase::Press };
    };

    class NativeUserEventBridge
    {
    public:
        using Submitter = std::function<void(const NativePadEvent&)>;

        static NativeUserEventBridge& GetSingleton();

        void SetSubmitter(Submitter fn);
        bool HasSubmitter() const;

        void Enqueue(TriggerCode code, TriggerPhase phase);
        void FlushQueued();  // 主线程调用（建议在 PlayerControls hook 里）

    private:
        mutable std::mutex _mtx;
        Submitter _submitter;
        std::vector<NativePadEvent> _queue;
    };
}