#include "pch.h"
#include "input/backend/ActionBackendPolicy.h"

#include "input/Action.h"

using namespace std::literals;

namespace dualpad::input::backend
{
    namespace
    {
        constexpr NativeButtonLifecycleHint MakeHoldOwnerPolicy(ButtonCommitGateClass gateClass)
        {
            return {
                .policy = ButtonCommitPolicy::HoldOwner,
                .minDownUs = 0,
                .minVisiblePolls = 1,
                .maxDeferredPolls = 0,
                .gateClass = gateClass
            };
        }

        constexpr NativeButtonLifecycleHint MakeDeferredPulsePolicy(ButtonCommitGateClass gateClass)
        {
            return {
                .policy = ButtonCommitPolicy::DeferredPulseWhenAllowed,
                .minDownUs = 0,
                .minVisiblePolls = 1,
                .maxDeferredPolls = 1,
                .gateClass = gateClass
            };
        }

        constexpr NativeButtonLifecycleHint MakeMinDownWindowPolicy(
            std::uint32_t minDownUs,
            ButtonCommitGateClass gateClass)
        {
            return {
                .policy = ButtonCommitPolicy::MinDownWindowPulse,
                .minDownUs = minDownUs,
                .minVisiblePolls = 1,
                .maxDeferredPolls = 1,
                .gateClass = gateClass
            };
        }

        constexpr bool IsPluginActionId(std::string_view actionId)
        {
            return actionId == actions::OpenInventory ||
                actionId == actions::OpenMagic ||
                actionId == actions::OpenMap ||
                actionId == actions::OpenJournal ||
                actionId == actions::OpenFavorites ||
                actionId == actions::OpenSkills ||
                actionId == actions::TogglePOV ||
                actionId == actions::ToggleHUD ||
                actionId == actions::Screenshot ||
                actionId == actions::Wait ||
                actionId == actions::QuickSave ||
                actionId == actions::QuickLoad;
        }

        constexpr NativeControlCode TryMapNativeButton(std::string_view actionId)
        {
            if (actionId == actions::Jump) {
                return NativeControlCode::Jump;
            }
            if (actionId == actions::Attack) {
                return NativeControlCode::Attack;
            }
            if (actionId == actions::Block) {
                return NativeControlCode::Block;
            }
            if (actionId == actions::Activate) {
                return NativeControlCode::Activate;
            }
            if (actionId == actions::Sprint) {
                return NativeControlCode::Sprint;
            }
            if (actionId == actions::Sneak) {
                return NativeControlCode::Sneak;
            }
            if (actionId == actions::Shout) {
                return NativeControlCode::Shout;
            }

            if (actionId == actions::MenuConfirm || actionId == "Console.Execute"sv) {
                return NativeControlCode::MenuConfirm;
            }
            if (actionId == actions::MenuCancel || actionId == "Book.Close"sv) {
                return NativeControlCode::MenuCancel;
            }
            if (actionId == actions::MenuScrollUp ||
                actionId == "Dialogue.PreviousOption"sv ||
                actionId == "Favorites.PreviousItem"sv ||
                actionId == "Console.HistoryUp"sv) {
                return NativeControlCode::MenuScrollUp;
            }
            if (actionId == actions::MenuScrollDown ||
                actionId == "Dialogue.NextOption"sv ||
                actionId == "Favorites.NextItem"sv ||
                actionId == "Console.HistoryDown"sv) {
                return NativeControlCode::MenuScrollDown;
            }
            if (actionId == actions::MenuLeft) {
                return NativeControlCode::MenuLeft;
            }
            if (actionId == actions::MenuRight) {
                return NativeControlCode::MenuRight;
            }
            if (actionId == actions::MenuPageUp ||
                actionId == "Book.PreviousPage"sv ||
                actionId == "Menu.SortByName"sv) {
                return NativeControlCode::MenuPageUp;
            }
            if (actionId == actions::MenuPageDown ||
                actionId == "Book.NextPage"sv ||
                actionId == "Menu.SortByValue"sv) {
                return NativeControlCode::MenuPageDown;
            }

            return NativeControlCode::None;
        }

        constexpr NativeControlCode TryMapNativeAxis(std::string_view actionId)
        {
            if (actionId == "Game.Move"sv) {
                return NativeControlCode::MoveStick;
            }
            if (actionId == "Game.Look"sv) {
                return NativeControlCode::LookStick;
            }
            if (actionId == "Menu.LeftStick"sv) {
                return NativeControlCode::MenuStick;
            }
            if (actionId == "Game.LeftTrigger"sv) {
                return NativeControlCode::LeftTriggerAxis;
            }
            if (actionId == "Game.RightTrigger"sv) {
                return NativeControlCode::RightTriggerAxis;
            }

            return NativeControlCode::None;
        }

        constexpr ButtonCommitGateClass ResolveGateClass(std::string_view actionId)
        {
            if (actionId == actions::Jump) {
                return ButtonCommitGateClass::GameplayJumping;
            }

            if (actionId == actions::Activate) {
                return ButtonCommitGateClass::GameplayActivate;
            }

            if (actionId == actions::Sprint) {
                return ButtonCommitGateClass::GameplayMovement;
            }

            if (actionId == actions::Sneak) {
                return ButtonCommitGateClass::GameplaySneaking;
            }

            if (actionId == actions::Attack ||
                actionId == actions::Block ||
                actionId == actions::Shout) {
                return ButtonCommitGateClass::GameplayFighting;
            }

            if (actionId == "Gameplay.Broad"sv) {
                return ButtonCommitGateClass::GameplayBroad;
            }

            if (actionId == actions::MenuConfirm ||
                actionId == actions::MenuCancel ||
                actionId == actions::MenuScrollUp ||
                actionId == actions::MenuScrollDown ||
                actionId == actions::MenuLeft ||
                actionId == actions::MenuRight ||
                actionId == actions::MenuPageUp ||
                actionId == actions::MenuPageDown ||
                actionId == "Console.Execute"sv ||
                actionId == "Book.Close"sv ||
                actionId == "Dialogue.PreviousOption"sv ||
                actionId == "Dialogue.NextOption"sv ||
                actionId == "Favorites.PreviousItem"sv ||
                actionId == "Favorites.NextItem"sv ||
                actionId == "Console.HistoryUp"sv ||
                actionId == "Console.HistoryDown"sv ||
                actionId == "Book.PreviousPage"sv ||
                actionId == "Book.NextPage"sv ||
                actionId == "Menu.SortByName"sv ||
                actionId == "Menu.SortByValue"sv) {
                return ButtonCommitGateClass::MenuControls;
            }

            return ButtonCommitGateClass::None;
        }

        constexpr NativeButtonLifecycleHint ResolveLifecyclePolicy(std::string_view actionId)
        {
            const auto gateClass = ResolveGateClass(actionId);
            if (actionId == actions::Jump) {
                return MakeMinDownWindowPolicy(70000, gateClass);
            }
            if (actionId == actions::Activate) {
                return MakeMinDownWindowPolicy(40000, gateClass);
            }
            if (actionId == actions::MenuConfirm ||
                actionId == actions::MenuCancel ||
                actionId == "Console.Execute"sv ||
                actionId == "Book.Close"sv) {
                return MakeDeferredPulsePolicy(gateClass);
            }

            return MakeHoldOwnerPolicy(gateClass);
        }
    }

    ActionRoutingDecision ActionBackendPolicy::Decide(std::string_view actionId)
    {
        if (IsPluginAction(actionId)) {
            return {
                .backend = PlannedBackend::Plugin,
                .kind = PlannedActionKind::PluginAction,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false,
                .lifecycle = {}
            };
        }

        if (IsLikelyModAction(actionId)) {
            return {
                .backend = PlannedBackend::ModEvent,
                .kind = PlannedActionKind::ModEvent,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false,
                .lifecycle = {}
            };
        }

        if (const auto button = TryMapNativeButton(actionId); button != NativeControlCode::None) {
            return {
                .backend = PlannedBackend::NativeState,
                .kind = PlannedActionKind::NativeButton,
                .nativeCode = button,
                .ownsLifecycle = true,
                .lifecycle = ResolveLifecyclePolicy(actionId)
            };
        }

        if (const auto axis = TryMapNativeAxis(actionId); axis != NativeControlCode::None) {
            const auto kind = (axis == NativeControlCode::LeftTriggerAxis || axis == NativeControlCode::RightTriggerAxis) ?
                PlannedActionKind::NativeAxis1D :
                PlannedActionKind::NativeAxis2D;
            return {
                .backend = PlannedBackend::NativeState,
                .kind = kind,
                .nativeCode = axis,
                .ownsLifecycle = true,
                .lifecycle = {}
            };
        }

        return {
            .backend = PlannedBackend::CompatibilityFallback,
            .kind = PlannedActionKind::NativeButton,
            .nativeCode = NativeControlCode::None,
            .ownsLifecycle = false,
            .lifecycle = {}
        };
    }

    bool ActionBackendPolicy::IsPluginAction(std::string_view actionId)
    {
        return IsPluginActionId(actionId);
    }

    bool ActionBackendPolicy::IsLikelyModAction(std::string_view actionId)
    {
        return actionId.starts_with("Mod."sv) ||
            actionId.starts_with("ModEvent"sv) ||
            actionId.starts_with("VirtualKey."sv) ||
            actionId.starts_with("FKey."sv);
    }
}
