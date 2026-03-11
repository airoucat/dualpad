#pragma once

#include "Common.h"

namespace dualpad::dinput8_proxy
{
    struct ProxyConfig
    {
        bool logOnlyInteresting{ true };
    };

    const ProxyConfig& GetProxyConfig();
}
