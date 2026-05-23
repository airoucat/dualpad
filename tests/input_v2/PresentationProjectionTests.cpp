#include "pch.h"

#include "input_v2/presentation/PresentationProjection.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <iostream>
#include <stdexcept>
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
        Require(compat.GamepadControlsCursorHook(), "pointer active KBM state should keep gamepad cursor compatibility enabled");

        compat.EnableRollback(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = true,
                .gamepadControlsCursor = false,
                .gamepadDeviceEnabled = true
            });
        Require(compat.IsUsingGamepadHook(), "rollback must return legacy compatibility output");
        Require(!compat.GamepadControlsCursorHook(), "rollback must not use projected cursor output");
        Require(
            compat.GetCommittedState().contextRevision == 9,
            "rollback must not mutate PH2-derived context truth in committed presentation state");

        compat.DisableRollback();
        const auto parity = compat.CompareShadowParity(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = false,
                .gamepadControlsCursor = true,
                .gamepadDeviceEnabled = false
            },
            true);
        Require(parity.passes, "shadow parity must pass when legacy and projected compatibility outputs match");
        Require(parity.contextRevision == 9, "shadow parity must carry contextRevision for diff logs");
        Require(parity.epoch == published.epoch, "shadow parity must carry epoch for refresh parity");

        const auto diff = compat.CompareShadowParity(
            presentation::LegacyCompatibilitySurface{
                .isUsingGamepad = true,
                .gamepadControlsCursor = true,
                .gamepadDeviceEnabled = false
            },
            true);
        Require(!diff.passes, "shadow parity must fail on projected hook output mismatch");
        Require(diff.diffs.size() == 1 && diff.diffs.front() == "isUsingGamepad", "shadow parity must report hook field diffs");
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
