#include "pch.h"
#include "input/injection/SourceBlockCoordinator.h"

#include "input/RuntimeConfig.h"
#include "input/protocol/DualSenseButtons.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::uint32_t kInterestingMenuBits =
            protocol::buttons::kCross |
            protocol::buttons::kCircle |
            protocol::buttons::kTriangle |
            protocol::buttons::kDpadUp |
            protocol::buttons::kDpadDown |
            protocol::buttons::kDpadLeft |
            protocol::buttons::kDpadRight;

        void MaybeLogSourceBlockChange(
            std::string_view op,
            std::uint32_t beforeMask,
            std::uint32_t afterMask,
            std::uint32_t argumentMask)
        {
            if (!RuntimeConfig::GetSingleton().LogMappingEvents()) {
                return;
            }

            const auto changedMask = (beforeMask ^ afterMask) & kInterestingMenuBits;
            if (changedMask == 0 && (argumentMask & kInterestingMenuBits) == 0) {
                return;
            }

            logger::debug(
                "[DualPad][MenuProbe] source-block op={} before=0x{:08X} after=0x{:08X} arg=0x{:08X} changed=0x{:08X}",
                op,
                beforeMask,
                afterMask,
                argumentMask,
                changedMask);
        }
    }

    void SourceBlockCoordinator::Reset()
    {
        const auto beforeMask = _blockedMask;
        _blockedMask = 0;
        MaybeLogSourceBlockChange("Reset", beforeMask, _blockedMask, beforeMask);
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
        const auto beforeMask = _blockedMask;
        _blockedMask |= sourceCode;
        MaybeLogSourceBlockChange("Block", beforeMask, _blockedMask, sourceCode);
    }

    void SourceBlockCoordinator::Release(std::uint32_t sourceCode)
    {
        const auto beforeMask = _blockedMask;
        _blockedMask &= ~sourceCode;
        MaybeLogSourceBlockChange("Release", beforeMask, _blockedMask, sourceCode);
    }

    void SourceBlockCoordinator::ReleaseMask(std::uint32_t releasedMask)
    {
        const auto beforeMask = _blockedMask;
        _blockedMask &= ~releasedMask;
        MaybeLogSourceBlockChange("ReleaseMask", beforeMask, _blockedMask, releasedMask);
    }
}
