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

    // Single public dispatch entry for protocol parsing. Callers should not branch
    // on concrete USB/BT parser details themselves.
    bool ParseDualSenseInputPacket(const RawInputPacket& packet, PadState& outState);
}
