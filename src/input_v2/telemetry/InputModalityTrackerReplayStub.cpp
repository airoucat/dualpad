#include "pch.h"
#include "input/InputModalityTracker.h"

#include "input_v2/actions/ActionSetResolver.h"
#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/gameplay/DualPadRuntime.h"
#include "input_v2/prompt/PromptRuntimeOwner.h"

namespace dualpad::input
{
    namespace
    {
        std::string_view ToReplayString(input_v2::presentation::PresentationOwner owner)
        {
            switch (owner) {
            case input_v2::presentation::PresentationOwner::Gamepad:
                return "Gamepad";
            case input_v2::presentation::PresentationOwner::KeyboardMouse:
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
            input_v2::gameplay::DualPadRuntime::GetSingleton().GetPublishedGameplayPresentation();

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
        input_v2::gameplay::DualPadRuntime::GetSingleton().ResetForTests();
        input_v2::prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
    }

    void InputModalityTracker::SetReplayContext(InputContext context, std::uint32_t epoch)
    {
        _observedContext.store(context, std::memory_order_relaxed);
        _observedContextEpoch.store(epoch, std::memory_order_relaxed);

        const auto bundle = input_v2::config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        const auto& catalog = bundle ? bundle->catalog : input_v2::context::ContextCatalog::BuiltInCatalog();
        const auto resolvedContext =
            input_v2::context::ContextCatalog::ResolveAlias(catalog, dualpad::input::ToString(context))
                .value_or(input_v2::context::UiContextId::None);

        input_v2::presentation::PublishedPresentationState presentation{};
        presentation.family = input_v2::presentation::DeviceFamily::Gamepad;
        presentation.uiContextId = resolvedContext;
        presentation.actionSetStack = input_v2::actions::ActionSetResolver::Resolve(catalog, resolvedContext);
        presentation.epoch = epoch == 0 ? 1 : epoch;
        input_v2::prompt::PromptRuntimeOwner::GetSingleton().PublishPresentationState(presentation);
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
