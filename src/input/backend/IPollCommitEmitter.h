#pragma once

#include "input_v2/compat/LegacyInputContextCompat.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    enum class EmitEdge : std::uint8_t
    {
        Down = 0,
        Up
    };

    struct EmitRequest
    {
        std::string_view actionId{};
        InputContext context{ InputContext::Gameplay };
        std::uint32_t epoch{ 0 };
        std::uint32_t tokenId{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
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
        // In the owner-generation native button commit mainline, Emit() is a
        // bookkeeping edge
        // acknowledgement used by PollCommitCoordinator token/state tracking.
        // It does not itself inject BSInputEvent queue records; the actual
        // gameplay-visible materialization happens later when the runtime owner
        // publishes committed virtual button current-state for the read-only
        // upstream gamepad hook.
        virtual EmitResult Emit(const EmitRequest& request) = 0;
    };
}
