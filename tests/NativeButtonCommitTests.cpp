#include "pch.h"

#include "input/backend/NativeButtonCommitBackend.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace dualpad::input;
    using namespace dualpad::input::backend;

    void Require(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void RunNativeDigitalGatePolicyContextTests()
    {
        constexpr std::array contexts{
            InputContext::Gameplay,
            InputContext::Menu,
            InputContext::FavoritesMenu,
            InputContext::Console,
            InputContext::Cursor,
            InputContext::Combat
        };

        for (const auto context : contexts) {
            Require(
                IsNativeDigitalGateOpenForContext(context),
                "native digital gate policy must not block context-local output");
        }
    }
}

int main()
{
    try {
        RunNativeDigitalGatePolicyContextTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
