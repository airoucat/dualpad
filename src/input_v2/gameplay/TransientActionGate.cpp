#include "pch.h"

#include "input_v2/gameplay/TransientActionGate.h"

namespace dualpad::input_v2::gameplay
{
    TransientActionGateDecision ResolveTransientActionGate(
        const TransientActionGateInput& input)
    {
        TransientActionGateDecision decision{ .next = input.previous };
        decision.next.key = input.key;
        decision.next.hasKey = true;

        if (input.physicalPress) {
            decision.next.physicalDown = true;
            if (input.virtualPress) {
                decision.virtualDisposition = input.virtualDownVisible ?
                    TransientGateDisposition::Cancel :
                    TransientGateDisposition::Suppress;
            }
        }
        if (input.physicalRelease) {
            decision.next.physicalDown = false;
        }
        if (input.virtualPress && decision.virtualDisposition == TransientGateDisposition::Keep) {
            decision.next.virtualDownVisible = true;
        }
        if (input.virtualRelease) {
            decision.next.virtualDownVisible = false;
        }
        if (decision.virtualDisposition == TransientGateDisposition::Cancel) {
            decision.next.virtualDownVisible = false;
        }
        return decision;
    }
}
