#pragma once

#include "input/InputContext.h"
#include "input/injection/AxisProjection.h"
#include "input/injection/SyntheticPadFrame.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    namespace backend
    {
        class FrameActionPlan;
    }

    class GameplayOwnershipCoordinator
    {
    public:
        enum class ChannelOwner : std::uint8_t
        {
            Gamepad,
            KeyboardMouse
        };

        struct OwnershipDecision
        {
            ProjectedAnalogState analog{};
            ChannelOwner lookOwner{ ChannelOwner::KeyboardMouse };
            ChannelOwner moveOwner{ ChannelOwner::KeyboardMouse };
            ChannelOwner combatOwner{ ChannelOwner::KeyboardMouse };
            ChannelOwner digitalOwner{ ChannelOwner::KeyboardMouse };
            bool lookSuppressed{ false };
            bool moveSuppressed{ false };
            bool leftTriggerSuppressed{ false };
            bool rightTriggerSuppressed{ false };
        };

        static GameplayOwnershipCoordinator& GetSingleton();

        void Reset();
        [[nodiscard]] ChannelOwner GetPublishedLookOwner() const;
        [[nodiscard]] ChannelOwner GetPublishedDigitalOwner() const;
        void UpdateDigitalOwnership(InputContext context, const backend::FrameActionPlan& framePlan);
        OwnershipDecision ApplyOwnership(
            const ProjectedAnalogState& analog,
            const SyntheticPadFrame& frame,
            InputContext context);

    private:
        GameplayOwnershipCoordinator() = default;

        void ResetForNonGameplay();
        void SetChannelOwner(
            ChannelOwner& channelSlot,
            std::string_view channelName,
            ChannelOwner owner,
            InputContext context,
            std::string_view reason);
        bool IsMeaningfulGamepadLook(const SyntheticPadFrame& frame) const;
        bool IsMeaningfulGamepadMove(const SyntheticPadFrame& frame) const;
        bool IsMeaningfulGamepadCombat(const SyntheticPadFrame& frame) const;
        bool HasMeaningfulGamepadDigitalAction(const backend::FrameActionPlan& framePlan) const;
        bool IsGameplayDomainContext(InputContext context) const;
        static std::string_view ToString(ChannelOwner owner);

        ChannelOwner _lookOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner _moveOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner _combatOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner _digitalOwner{ ChannelOwner::KeyboardMouse };
        std::atomic<ChannelOwner> _publishedLookOwner{ ChannelOwner::KeyboardMouse };
        std::atomic<ChannelOwner> _publishedDigitalOwner{ ChannelOwner::KeyboardMouse };
    };
}
