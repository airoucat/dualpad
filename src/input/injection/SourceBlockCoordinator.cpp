#include "pch.h"
#include "input/injection/SourceBlockCoordinator.h"

namespace dualpad::input
{
    void SourceBlockCoordinator::Reset()
    {
        _blockedMask = 0;
    }

    std::uint32_t SourceBlockCoordinator::CurrentMask() const
    {
        return _blockedMask;
    }

    bool SourceBlockCoordinator::IsBlocked(std::uint32_t sourceCode) const
    {
        return (_blockedMask & sourceCode) != 0;
    }

    void SourceBlockCoordinator::Block(std::uint32_t sourceCode)
    {
        _blockedMask |= sourceCode;
    }

    void SourceBlockCoordinator::Release(std::uint32_t sourceCode)
    {
        _blockedMask &= ~sourceCode;
    }

    void SourceBlockCoordinator::ReleaseMask(std::uint32_t releasedMask)
    {
        _blockedMask &= ~releasedMask;
    }
}
