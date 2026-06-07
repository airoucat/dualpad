#include "pch.h"

#include "input_v2/presentation/PresentationProjection.h"
#include "input_v2/presentation/GameplayPresentationAdapter.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    dualpad::input_v2::context::ResolvedContextSnapshot GameplayContext()
    {
        dualpad::input_v2::context::ResolvedContextSnapshot context{};
        context.hostMode = dualpad::input_v2::context::HostMode::Gameplay;
        context.uiContextId = dualpad::input_v2::context::UiContextId::None;
        context.actionSetStack.baseSetId = "GameplayBase";
        context.presentationPolicyId = "GameplayPolicyFromPH2";
        context.contextRevision = 7;
        return context;
    }

    dualpad::input_v2::context::ResolvedContextSnapshot MenuContext()
    {
        auto context = GameplayContext();
        context.hostMode = dualpad::input_v2::context::HostMode::Menu;
        context.uiContextId = dualpad::input_v2::context::UiContextId::Journal;
        context.actionSetStack.baseSetId = "MenuBase";
        context.actionSetStack.layerIds = { "JournalLayer" };
        context.actionSetStack.scopeAnchorIds = { "MenuBase", "JournalLayer" };
        context.presentationPolicyId = "PolicyOnlyPH2MayChoose";
        context.contextRevision = 8;
        context.menuStackRevision = 4;
        return context;
    }
}

void RunPresentationProjectionTests()
{
    namespace presentation = dualpad::input_v2::presentation;

    {
        presentation::DeviceFamilyIngressPublisher ingress;
        presentation::SourceEvidenceCollector collector;

        auto publication = ingress.Publish(
            presentation::DeviceFamily::Gamepad,
            presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            100);
        auto frame = collector.CollectAfterDeviceFamilyIngress(publication, GameplayContext(), 101);

        Require(frame.records.size() == 2, "device family change must publish marker followed by source evidence snapshot");
        Require(
            frame.records[0].kind == presentation::SourceEvidenceRecordKind::DeviceFamilyChanged,
            "first record must be DeviceFamilyChanged marker");
        Require(
            frame.records[1].kind == presentation::SourceEvidenceRecordKind::SourceEvidenceSnapshot,
            "second record must be SourceEvidenceSnapshot");
        Require(
            frame.records[0].deviceFamilyChanged.newRevision ==
                frame.records[1].sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision,
            "DeviceFamilyChangedPayload.newRevision must equal paired SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision");
        Require(
            frame.records[1].sourceEvidence.contextRevision == 7,
            "SourceEvidenceSnapshot must pair with the PH2 resolved context revision");

        collector.MarkSyntheticKeyboardScancode(42, 1, 100, 1000);
        Require(collector.IsSyntheticKeyboardWindowActive(1050), "collector must own synthetic keyboard window state");
        Require(collector.ConsumeSyntheticKeyboardScancode(42, 1050), "collector must consume pending synthetic keyboard scancode");
        collector.RecordKeyboardEvidence(true, false, 1060);
        collector.RecordMouseButtonEvidence(true, 1061);
        collector.RecordMouseMoveEvidence(6, 6, 1062);
        collector.RecordGamepadEvidence(true, 1063, 1500);
        Require(collector.HasGamepadLeaseActive(1200), "collector must own gamepad lease state");
        Require(collector.ShouldPromoteMouseMove(10, 120, 1200), "collector must own mouse move accumulator state");
        collector.ResetForContextBoundary(1201);
        Require(!collector.ShouldPromoteMouseMove(10, 120, 1400), "context boundary must reset mouse move evidence");
        Require(collector.GetLatestSnapshot().pointerSignal == presentation::PointerSignal::None, "context boundary reset must clear pointer signal");
    }

    {
        presentation::DeviceFamilyIngressPublisher ingress;
        presentation::SourceEvidenceCollector collector;
        presentation::PresentationProjection projection;

        const auto frame = collector.CollectAfterDeviceFamilyIngress(
            ingress.Publish(
                presentation::DeviceFamily::Gamepad,
                presentation::DeviceFamilyEvidenceSource::RawInputIngress,
                200),
            MenuContext(),
            201);
        const auto& snapshot = frame.records.back().sourceEvidence;

        presentation::PublishedGameplayPresentation gameplay{};
        gameplay.engineOwner = presentation::PresentationOwner::KeyboardMouse;
        gameplay.menuEntryOwner = presentation::PresentationOwner::Gamepad;
        gameplay.gameplayPresentationRevision = 3;

        const auto published = projection.Project(snapshot, MenuContext(), gameplay);
        Require(published.family == presentation::DeviceFamily::Gamepad, "projection must copy family from source evidence");
        Require(
            published.deviceFamilyRevision == snapshot.deviceFamilyEvidence.deviceFamilyRevision,
            "projection must copy deviceFamilyRevision from source evidence");
        Require(
            published.owner == presentation::PresentationOwner::Gamepad,
            "gameplay to menu first projection must inherit PublishedGameplayPresentation.menuEntryOwner");
        Require(
            published.presentationPolicyId == "PolicyOnlyPH2MayChoose",
            "PresentationProjection must forward PH2 presentationPolicyId without deriving it from menu semantics");
        Require(
            published.uiContextId == MenuContext().uiContextId,
            "PresentationProjection must forward PH2 uiContextId");
        Require(
            published.actionSetStack == MenuContext().actionSetStack,
            "PresentationProjection must forward PH2 action set stack");
        Require(published.epoch == 1, "dirty first publish must advance epoch");

        auto keyboardTakeover = snapshot;
        keyboardTakeover.keyboardEvidence = true;
        keyboardTakeover.deviceFamilyEvidence.family = presentation::DeviceFamily::KeyboardMouse;
        keyboardTakeover.deviceFamilyEvidence.deviceFamilyRevision = 2;
        const auto keyboardPublished = projection.Project(keyboardTakeover, MenuContext(), gameplay);
        Require(
            keyboardPublished.owner == presentation::PresentationOwner::KeyboardMouse,
            "menu keyboard evidence must take over owner from inherited gamepad");
        Require(
            presentation::HasDirtyFlag(keyboardPublished.dirty, presentation::PresentationDirtyFlags::Owner),
            "menu keyboard takeover must set dirty.Owner");
        Require(keyboardPublished.epoch == published.epoch + 1, "menu keyboard takeover must advance epoch");

        const auto unchanged = projection.Project(keyboardTakeover, MenuContext(), gameplay);
        Require(unchanged.owner == presentation::PresentationOwner::KeyboardMouse, "unchanged menu publish must keep owner");
        Require(unchanged.dirty == presentation::PresentationDirtyFlags::None, "unchanged menu publish must not set dirty flags");
        Require(unchanged.epoch == keyboardPublished.epoch, "unchanged menu publish must not jitter epoch");

        auto gamepadReclaim = keyboardTakeover;
        gamepadReclaim.keyboardEvidence = false;
        gamepadReclaim.gamepadEvidence = true;
        gamepadReclaim.gamepadLease = true;
        gamepadReclaim.deviceFamilyEvidence.family = presentation::DeviceFamily::Gamepad;
        gamepadReclaim.deviceFamilyEvidence.deviceFamilyRevision = 3;
        const auto gamepadPublished = projection.Project(gamepadReclaim, MenuContext(), gameplay);
        Require(
            gamepadPublished.owner == presentation::PresentationOwner::Gamepad,
            "menu gamepad evidence or lease must reclaim owner");
        Require(
            presentation::HasDirtyFlag(gamepadPublished.dirty, presentation::PresentationDirtyFlags::Owner),
            "menu gamepad reclaim must set dirty.Owner");
        Require(gamepadPublished.epoch == unchanged.epoch + 1, "menu gamepad reclaim must advance epoch");

        gameplay.engineOwner = presentation::PresentationOwner::KeyboardMouse;
        const auto gameplayPublished = projection.Project(gamepadReclaim, GameplayContext(), gameplay);
        Require(
            gameplayPublished.owner == presentation::PresentationOwner::KeyboardMouse,
            "menu to gameplay must consume PublishedGameplayPresentation.engineOwner");
    }

    {
        presentation::PresentationProjection projection;
        presentation::SkyrimCompatibilitySurface compat;
        presentation::SourceEvidenceSnapshot snapshot{};
        snapshot.deviceFamilyEvidence = presentation::PublishedDeviceFamilyEvidence{
            .family = presentation::DeviceFamily::KeyboardMouse,
            .deviceFamilyRevision = 12,
            .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            .publishedTick = 300
        };
        snapshot.pointerSignal = presentation::PointerSignal::PointerActive;
        snapshot.contextRevision = 9;

        presentation::PublishedGameplayPresentation gameplay{};
        gameplay.engineOwner = presentation::PresentationOwner::KeyboardMouse;
        gameplay.menuEntryOwner = presentation::PresentationOwner::KeyboardMouse;

        auto context = GameplayContext();
        context.contextRevision = 9;
        const auto published = projection.Project(snapshot, context, gameplay);
        compat.Commit(published);
        Require(!compat.IsUsingGamepadHook(), "compat hook must read committed published owner");
        Require(!compat.GamepadControlsCursorHook(), "pointer active KBM state must publish KeyboardMouse cursor owner");

        compat.EnableRollback(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = true,
                .gamepadControlsCursor = false,
                .gamepadDeviceEnabled = true
            });
        Require(!compat.IsUsingGamepadHook(), "rollback helper must not override committed input_v2 owner");
        Require(!compat.GamepadControlsCursorHook(), "rollback helper must not override committed cursor output");
        Require(
            compat.GetCommittedState().contextRevision == 9,
            "rollback must not mutate PH2-derived context truth in committed presentation state");

        compat.DisableRollback();
        const auto parity = compat.CompareShadowParity(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = false,
                .gamepadControlsCursor = false,
                .gamepadDeviceEnabled = false
            },
            true);
        Require(parity.passes, "shadow parity must pass when legacy and projected compatibility outputs match");
        Require(parity.contextRevision == 9, "shadow parity must carry contextRevision for diff logs");
        Require(parity.epoch == published.epoch, "shadow parity must carry epoch for refresh parity");

        const auto diff = compat.CompareShadowParity(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = true,
                .gamepadControlsCursor = false,
                .gamepadDeviceEnabled = false
            },
            true);
        Require(!diff.passes, "shadow parity must fail on projected hook output mismatch");
        Require(diff.diffs.size() == 1 && diff.diffs.front() == "isUsingGamepad", "shadow parity must report hook field diffs");
    }

    {
        const auto site = presentation::detail::MakeVfuncPatchSite(0x1000, 0x8);
        Require(
            site.relocationBase == 0x1000,
            "vfunc hook site must pass the vtable base to write_vfunc instead of a pre-offset slot address");
        Require(site.index == 0x8, "vfunc hook site must carry the single vfunc index offset");

        auto state = presentation::detail::InstallState::NotInstalled;
        Require(presentation::detail::CanBeginInstall(state), "fresh install state must allow install start");
        state = presentation::detail::BeginInstall(state);
        Require(state == presentation::detail::InstallState::Installing, "begin install must enter Installing state");
        state = presentation::detail::FailInstall(state);
        Require(state == presentation::detail::InstallState::Failed, "failed install attempt must enter Failed state");
        Require(!presentation::detail::CanBeginInstall(state), "failed install state must not silently retry");

        const auto success = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::Success,
            "installed");
        Require(success.installed, "success hook install result must be installed");
        Require(!presentation::IsHookInstallFailure(success), "success hook install result must not fail closed");
        Require(
            presentation::ToString(success.status) == std::string_view("success"),
            "success hook install result must expose a stable debug status");

        const auto unsupported = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::UnsupportedRuntime,
            "unsupported_runtime_1.6.640");
        Require(!unsupported.installed, "unsupported runtime hook result must not be installed");
        Require(presentation::IsHookInstallFailure(unsupported), "unsupported runtime must fail closed");
        Require(
            presentation::ToDebugString(unsupported).find("unsupported_runtime") != std::string::npos,
            "unsupported runtime hook result must expose debug reason");

        const auto mismatch = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::SignatureMismatch,
            "is_using_gamepad_call_signature_mismatch");
        Require(!mismatch.installed, "signature mismatch hook result must not be installed");
        Require(presentation::IsHookInstallFailure(mismatch), "signature mismatch must fail closed");

        const auto alreadyInstalled = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::AlreadyInstalled,
            "install_already_completed");
        Require(alreadyInstalled.installed, "already installed hook result must remain installed");
        Require(!presentation::IsHookInstallFailure(alreadyInstalled), "already installed must not fail closed");

        const auto failed = presentation::detail::EvaluateHookPatchFailure(
            presentation::detail::HookInstallProgress::NotStarted,
            "exception_before_patch_started");
        Require(failed.status == presentation::HookInstallStatus::Failed, "pre-patch exception must be failed");
        Require(presentation::IsHookInstallFailure(failed), "failed hook install must fail closed");

        const auto partial = presentation::detail::EvaluateHookPatchFailure(
            presentation::detail::HookInstallProgress::PatchStarted,
            "exception_after_patch_started");
        Require(
            partial.status == presentation::HookInstallStatus::PartialInstall,
            "post-patch exception must be partial install");
        Require(presentation::IsHookInstallFailure(partial), "partial hook install must fail closed");

        Require(
            presentation::detail::EvaluateHookInstallGate(true, true).status ==
                presentation::HookInstallStatus::Success,
            "matching runtime and signatures may install");
        Require(
            presentation::detail::EvaluateHookInstallGate(false, true).status ==
                presentation::HookInstallStatus::UnsupportedRuntime,
            "unsupported runtime must stop before patching");
        Require(
            presentation::detail::EvaluateHookInstallGate(true, false).status ==
                presentation::HookInstallStatus::SignatureMismatch,
            "signature mismatch must stop before patching");
    }

    {
        presentation::GameplayPresentationAdapter adapter;
        const auto first = adapter.PublishForTests(
            presentation::GameplayPresentationAdapterInput{
                .engineOwner = presentation::PresentationOwner::Gamepad,
                .menuEntryOwner = presentation::PresentationOwner::Gamepad,
                .reason = presentation::GameplayPresentationReasonCode::CoordinatorPublished,
                .publishedTick = 1
            });
        Require(first.gameplayPresentationRevision == 1, "adapter must increment gameplayPresentationRevision on published owner changes");

        const auto unchanged = adapter.PublishForTests(
            presentation::GameplayPresentationAdapterInput{
                .engineOwner = presentation::PresentationOwner::Gamepad,
                .menuEntryOwner = presentation::PresentationOwner::Gamepad,
                .reason = presentation::GameplayPresentationReasonCode::CoordinatorPublished,
                .publishedTick = 2
            });
        Require(unchanged.gameplayPresentationRevision == 1, "adapter must not increment gameplayPresentationRevision on unchanged published state");

        const auto resynced = adapter.PublishCleanBaselineForTests(3);
        Require(resynced.gameplayPresentationRevision == 2, "adapter explicit clean baseline must increment gameplayPresentationRevision");
    }
}

int main()
{
    try {
        RunPresentationProjectionTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
