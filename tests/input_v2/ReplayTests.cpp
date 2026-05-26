#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressMarkers.h"
#include "input_v2/ingress/IngressRecovery.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{
    using namespace dualpad::input_v2;

    void Require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    ingress::IngressEvent Event(ingress::IngressKind kind, std::uint64_t seq)
    {
        ingress::IngressEvent event{};
        event.kind = kind;
        event.seq = seq;
        event.monotonicUs = seq * 10;
        return event;
    }

    ingress::IngressEvent Manifest(std::uint64_t seq, std::uint32_t epoch)
    {
        auto event = Event(ingress::IngressKind::ManifestEpochChanged, seq);
        event.manifest.manifestEpoch = epoch;
        return event;
    }

    ingress::IngressEvent Ui(std::uint64_t seq, std::uint32_t contextRevision, std::uint32_t menuStackRevision)
    {
        auto event = Event(ingress::IngressKind::UiSnapshot, seq);
        event.ui.contextRevision = contextRevision;
        event.ui.menuStackRevision = menuStackRevision;
        return event;
    }

    ingress::IngressEvent Device(std::uint64_t seq, std::uint32_t revision)
    {
        auto event = Event(ingress::IngressKind::DeviceFamilyChanged, seq);
        event.deviceFamily.family = presentation::DeviceFamily::Gamepad;
        event.deviceFamily.deviceFamilyRevision = revision;
        return event;
    }

    ingress::IngressEvent Evidence(std::uint64_t seq, std::uint32_t revision)
    {
        auto event = Event(ingress::IngressKind::SourceEvidence, seq);
        event.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision = revision;
        return event;
    }

    const ingress::AssembledFactFrame* FindTransition(
        const std::vector<ingress::AssembledFactFrame>& frames,
        ingress::TransitionReason reason)
    {
        for (const auto& frame : frames) {
            if (frame.kind == ingress::AssembledFrameKind::Transition &&
                frame.transition.reason == reason) {
                return &frame;
            }
        }
        return nullptr;
    }

    std::filesystem::path FindProjectRoot(std::filesystem::path from = std::filesystem::current_path())
    {
        while (!from.empty()) {
            if (std::filesystem::is_regular_file(from / "xmake.lua")) {
                return from;
            }
            const auto parent = from.parent_path();
            if (parent == from) {
                break;
            }
            from = parent;
        }
        std::cerr << "FAIL: could not find project root\n";
        std::exit(1);
    }

    void TestPhase0MandatoryReplayCoverageRemainsTenScenarios()
    {
        const auto root = FindProjectRoot();
        const auto phase0 = root / "tests" / "replay" / "golden" / "phase0";
        std::size_t scenarioCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(phase0)) {
            if (entry.is_directory()) {
                ++scenarioCount;
            }
        }
        Require(scenarioCount == 10, "phase0 mandatory replay coverage must remain 10 scenarios");
    }

    void TestManifestReloadReplayProducesHardResetTransition()
    {
        ingress::FrameAssembler assembler;
        const auto frames = assembler.Assemble({
            Manifest(1, 1),
            Ui(2, 1, 1),
            Device(3, 1),
            Evidence(4, 1),
            Manifest(5, 2),
            Ui(6, 1, 1),
            Evidence(7, 1)
        });

        const auto* reload = FindTransition(frames, ingress::TransitionReason::ManifestEpochChanged);
        Require(reload != nullptr, "manifest reload transition must be separate");
        Require(reload->transition.requestHardResync, "manifest reload must request hard reset");
        Require(ToGameplayRecoveryInput(*reload).hardResetRequested, "manifest replay maps to hard recovery input");
        Require(frames.back().boundaryKey.manifestEpoch == 2, "stable frame after reload uses marker payload epoch");
    }

    void TestReplaySequenceGapDoesNotReachStableConsumer()
    {
        ingress::FrameAssembler assembler;
        auto gap = ingress::MakeSequenceGapEvent();
        gap.seq = 5;
        gap.monotonicUs = 50;
        const auto frames = assembler.Assemble({
            Manifest(1, 1),
            Ui(2, 1, 1),
            Device(3, 1),
            Evidence(4, 1),
            gap
        });

        const auto* gapFrame = FindTransition(frames, ingress::TransitionReason::SequenceGap);
        Require(gapFrame != nullptr, "gap must be transition");
        Require(!ingress::ShouldDispatchToInteractionEngine(*gapFrame), "gap transition must not dispatch to interaction engine");
        Require(ToGameplayRecoveryInput(*gapFrame).sequenceGapObserved, "gap recovery marker must be preserved");
    }
}

int main()
{
    TestPhase0MandatoryReplayCoverageRemainsTenScenarios();
    TestManifestReloadReplayProducesHardResetTransition();
    TestReplaySequenceGapDoesNotReachStableConsumer();
    std::cout << "DualPadReplayTests passed\n";
    return 0;
}
