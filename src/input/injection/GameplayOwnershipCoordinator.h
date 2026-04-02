#pragma once

#include "input/InputContext.h"
#include "input/injection/AxisProjection.h"
#include "input/injection/SyntheticPadFrame.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    struct GameplayKbmFacts;

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

        struct DigitalGatePlan
        {
            bool suppressNewTransientActions{ false };
            bool cancelExistingTransientActions{ false };
        };

        struct GameplayPresentationState
        {
            ChannelOwner engineOwner{ ChannelOwner::KeyboardMouse };
            ChannelOwner menuEntryOwner{ ChannelOwner::KeyboardMouse };
        };

        enum class PresentationHint : std::uint8_t
        {
            KeyboardMouseExplicit,
            KeyboardMouseLookOnly,
            GamepadExplicit,
            GamepadMoveOnly
        };

        static GameplayOwnershipCoordinator& GetSingleton();

        void Reset();
        [[nodiscard]] ChannelOwner GetPublishedLookOwner() const;
        [[nodiscard]] ChannelOwner GetPublishedDigitalOwner() const;
        [[nodiscard]] ChannelOwner GetPublishedGameplayPresentationOwner() const;
        [[nodiscard]] ChannelOwner GetPublishedGameplayMenuEntryOwner() const;
        [[nodiscard]] GameplayPresentationState GetPublishedGameplayPresentationState() const;
        void RecordGameplayPresentationHint(
            InputContext context,
            PresentationHint hint,
            std::string_view reason);
        void RefreshPublishedGameplayPresentation(InputContext context);
        [[nodiscard]] DigitalGatePlan UpdateDigitalOwnership(
            InputContext context,
            const backend::FrameActionPlan& framePlan);
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
        void RefreshPresentationLease(PresentationHint hint, std::string_view reason);
        bool IsPresentationLeaseActive(PresentationHint hint, std::uint64_t nowMs) const;
        void UpdatePublishedGameplayPresentationState(
            InputContext context,
            const GameplayKbmFacts& facts);
        bool IsGameplayDomainContext(InputContext context) const;
        static std::string_view ToString(ChannelOwner owner);
        static std::string_view ToString(PresentationHint hint);

        ChannelOwner _lookOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner _moveOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner _combatOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner _digitalOwner{ ChannelOwner::KeyboardMouse };
        std::atomic<ChannelOwner> _publishedLookOwner{ ChannelOwner::KeyboardMouse };
        std::atomic<ChannelOwner> _publishedDigitalOwner{ ChannelOwner::KeyboardMouse };
        std::atomic<ChannelOwner> _publishedGameplayPresentationOwner{ ChannelOwner::KeyboardMouse };
        std::atomic<ChannelOwner> _publishedGameplayMenuEntryOwner{ ChannelOwner::KeyboardMouse };
        std::uint64_t _keyboardMouseExplicitLeaseUntilMs{ 0 };
        std::uint64_t _keyboardMouseLookLeaseUntilMs{ 0 };
        std::uint64_t _gamepadExplicitLeaseUntilMs{ 0 };
        std::uint64_t _gamepadMoveOnlyLeaseUntilMs{ 0 };
    };
}
