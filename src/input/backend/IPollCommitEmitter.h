#pragma once

#include "input/InputContext.h"

#include <cstdint>

namespace dualpad::input::backend
{
    enum class EmitEdge : std::uint8_t
    {
        Down = 0,
        Up
    };

    struct EmitRequest
    {
        RE::BSFixedString actionId{};
        InputContext context{ InputContext::Gameplay };
        std::uint32_t epoch{ 0 };
        std::uint32_t tokenId{ 0 };
        EmitEdge edge{ EmitEdge::Down };
        float heldSeconds{ 0.0f };
        std::uint64_t nowUs{ 0 };
    };

    struct EmitResult
    {
        bool submitted{ false };
        bool queueFull{ false };
        bool transientBlocked{ false };
    };

    class IPollCommitEmitter
    {
    public:
        virtual ~IPollCommitEmitter() = default;
        // In the poll-owned native button commit mainline, Emit() is a
        // bookkeeping edge
        // acknowledgement used by PollCommitCoordinator token/state tracking.
        // It does not itself inject BSInputEvent queue records; the actual
        // gameplay-visible materialization happens later when the poll path
        // exports committed virtual button current-state to the upstream
        // gamepad hook.
        virtual EmitResult Emit(const EmitRequest& request) = 0;
    };
}
