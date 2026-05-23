#include "pch.h"
#include "input/InputModalityTracker.h"

#include "input/injection/GameplayOwnershipCoordinator.h"

namespace dualpad::input
{
    namespace
    {
        std::string_view ToReplayString(GameplayOwnershipCoordinator::ChannelOwner owner)
        {
            switch (owner) {
            case GameplayOwnershipCoordinator::ChannelOwner::Gamepad:
                return "Gamepad";
            case GameplayOwnershipCoordinator::ChannelOwner::KeyboardMouse:
            default:
                return "KeyboardMouse";
            }
        }
    }

    InputModalityTracker& InputModalityTracker::GetSingleton()
    {
        static InputModalityTracker instance;
        return instance;
    }

    InputModalityTracker::InputModalityTracker() = default;

    void InputModalityTracker::Install()
    {
    }

    void InputModalityTracker::Register()
    {
    }

    void InputModalityTracker::OnAuthoritativeMenuOpen(InputContext context, std::uint32_t epoch)
    {
        SetReplayContext(context, epoch);
    }

    bool InputModalityTracker::IsInstalled() const
    {
        return false;
    }

    bool InputModalityTracker::IsUsingGamepad() const
    {
        return false;
    }

    bool InputModalityTracker::IsGameplayUsingGamepad() const
    {
        return false;
    }

    bool InputModalityTracker::IsGameplayMenuEntrySeedGamepad() const
    {
        return false;
    }

    InputModalityTracker::ReplayCompatibilitySurface InputModalityTracker::CaptureCompatibilitySurfaceForReplay() const
    {
        const auto gameplayPresentation =
            GameplayOwnershipCoordinator::GetSingleton().GetPublishedGameplayPresentationState();

        return ReplayCompatibilitySurface{
            .context = _observedContext.load(std::memory_order_relaxed),
            .contextEpoch = _observedContextEpoch.load(std::memory_order_relaxed),
            .isUsingGamepad = false,
            .gamepadControlsCursor = false,
            .gamepadDeviceEnabled = false,
            .presentationOwner = "KeyboardMouse",
            .cursorOwner = "KeyboardMouse",
            .gameplayEngineOwner = std::string(ToReplayString(gameplayPresentation.engineOwner)),
            .gameplayMenuEntryOwner = std::string(ToReplayString(gameplayPresentation.menuEntryOwner))
        };
    }

    void InputModalityTracker::ResetForReplayCapture()
    {
        _observedContext.store(InputContext::Gameplay, std::memory_order_relaxed);
        _observedContextEpoch.store(0, std::memory_order_relaxed);
    }

    void InputModalityTracker::SetReplayContext(InputContext context, std::uint32_t epoch)
    {
        _observedContext.store(context, std::memory_order_relaxed);
        _observedContextEpoch.store(epoch, std::memory_order_relaxed);
    }

    void InputModalityTracker::MarkSyntheticKeyboardScancode(std::uint8_t, std::uint8_t, std::uint64_t)
    {
    }

    RE::BSEventNotifyControl InputModalityTracker::ProcessEvent(
        RE::InputEvent* const*,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        return RE::BSEventNotifyControl::kContinue;
    }
}
