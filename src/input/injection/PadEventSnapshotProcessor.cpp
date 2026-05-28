#include "pch.h"

#include "input/injection/PadEventSnapshotProcessor.h"

#include "input/AuthoritativePollState.h"
#include "input/backend/KeyboardHelperBackend.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input_v2/gameplay/DualPadRuntime.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/telemetry/InputTraceRecorder.h"

namespace dualpad::input
{
    namespace
    {
        input_v2::ingress::FrameAssembler& DirectProcessorAssembler()
        {
            static input_v2::ingress::FrameAssembler assembler;
            return assembler;
        }
    }

    PadEventSnapshotProcessor& PadEventSnapshotProcessor::GetSingleton()
    {
        static PadEventSnapshotProcessor instance;
        return instance;
    }

    void PadEventSnapshotProcessor::ResetState()
    {
        AuthoritativePollState::GetSingleton().Reset();
        backend::NativeButtonCommitBackend::GetSingleton().Reset();
        backend::KeyboardHelperBackend::GetSingleton().Reset();
        input_v2::gameplay::DualPadRuntime::GetSingleton().ResetForTests();
        input_v2::ingress::IngressHub::GetSingleton().ResetForTests();
        DirectProcessorAssembler().Reset();
    }

    void PadEventSnapshotProcessor::Process(const PadEventSnapshot& snapshot)
    {
        if (snapshot.type == PadEventSnapshotType::Reset) {
            ResetState();
            return;
        }

        auto& hub = input_v2::ingress::IngressHub::GetSingleton();
        (void)hub.PushPadSnapshot(snapshot);
        const auto events = hub.Drain();
        const auto frames = DirectProcessorAssembler().Assemble(events);
        for (const auto& frame : frames) {
            ProcessIngressFrame(frame);
        }
    }

    void PadEventSnapshotProcessor::ProcessIngressFrame(const input_v2::ingress::AssembledFactFrame& frame)
    {
        if (frame.kind == input_v2::ingress::AssembledFrameKind::Transition &&
            frame.transition.requestHardResync) {
            AuthoritativePollState::GetSingleton().Reset();
        }

        (void)input_v2::gameplay::DualPadRuntime::GetSingleton().ProcessAssembledFrame(frame);
        if (frame.kind == input_v2::ingress::AssembledFrameKind::Stable && frame.facts.legacySnapshot) {
            const auto& snapshot = *frame.facts.legacySnapshot;
            auto& pollState = AuthoritativePollState::GetSingleton();
            pollState.PublishFrameMetadata(
                snapshot.sourceTimestampUs,
                snapshot.overflowed,
                snapshot.coalesced);
            pollState.PublishUnmanagedDigitalEdges(0, 0, 0);

            const auto gameplayPresentation =
                input_v2::gameplay::DualPadRuntime::GetSingleton().GetPublishedGameplayPresentation();
            input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordProcessedSnapshot(
                snapshot,
                pollState.ReadSnapshot(),
                input_v2::telemetry::ReplayCompatibilitySurface{
                    .context = snapshot.context,
                    .contextEpoch = snapshot.contextEpoch,
                    .isUsingGamepad = false,
                    .gamepadControlsCursor = false,
                    .gamepadDeviceEnabled = false,
                    .presentationOwner = "KeyboardMouse",
                    .cursorOwner = "KeyboardMouse",
                    .gameplayEngineOwner = gameplayPresentation.engineOwner == input_v2::presentation::PresentationOwner::Gamepad ?
                        "Gamepad" :
                        "KeyboardMouse",
                    .gameplayMenuEntryOwner = gameplayPresentation.menuEntryOwner == input_v2::presentation::PresentationOwner::Gamepad ?
                        "Gamepad" :
                        "KeyboardMouse"
                });
        }
    }
}
