#pragma once

#include "input/protocol/DualSenseProtocolTypes.h"
#include "input/state/PadState.h"

namespace dualpad::input
{
    class IDualSenseInputParser
    {
    public:
        virtual ~IDualSenseInputParser() = default;
        virtual bool Parse(const RawInputPacket& packet, PadState& outState) = 0;
    };

    bool ParseDualSenseInputPacket(const RawInputPacket& packet, PadState& outState);
}
