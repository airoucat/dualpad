#pragma once

#include <cstdint>

namespace dualpad::input
{
    class SourceBlockCoordinator
    {
    public:
        void Reset();

        [[nodiscard]] std::uint32_t CurrentMask() const;
        [[nodiscard]] bool IsBlocked(std::uint32_t sourceCode) const;

        void Block(std::uint32_t sourceCode);
        void Release(std::uint32_t sourceCode);
        void ReleaseMask(std::uint32_t releasedMask);

    private:
        std::uint32_t _blockedMask{ 0 };
    };
}
