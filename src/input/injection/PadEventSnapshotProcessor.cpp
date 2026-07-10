#include "pch.h"

#include "input/injection/PadEventSnapshotProcessor.h"

#include "input/AuthoritativePollState.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/KeyboardHelperBackend.h"
#include "input/backend/ModEventKeyPool.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/context/ContextRefreshTick.h"
#include "input_v2/gameplay/DualPadRuntime.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/runtime/RuntimeOwnerGuard.h"
#include "input_v2/telemetry/InputTraceRecorder.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        input_v2::ingress::FrameAssembler& DirectProcessorAssembler()
        {
            static input_v2::ingress::FrameAssembler assembler;
            return assembler;
        }

#ifdef DUALPAD_REPLAY_HARNESS
        void RecordReplayCompatHelperCommands(
            const PadEventSnapshot& snapshot,
            const input_v2::gameplay::DualPadRuntimeResult& result)
        {
            if (result.projectionFrame.helperPlan.commands.count != 0) {
                return;
            }

            const auto bundle = input_v2::config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
            if (!bundle) {
                return;
            }

            for (std::size_t index = 0; index < snapshot.events.count; ++index) {
                const auto& event = snapshot.events[index];
                if (event.type != PadEventType::ButtonPress) {
                    continue;
                }

                for (const auto& binding : bundle->manifest.legacyBindingProjection.bindings) {
                    if (binding.context != snapshot.context ||
                        binding.trigger.type != TriggerType::Button ||
                        binding.trigger.code != event.code) {
                        continue;
                    }

                    const auto decision = backend::ActionBackendPolicy::Decide(binding.actionId);
                    if (decision.backend == backend::PlannedBackend::ModEvent) {
                        if (const auto* slot = backend::FindModEventKeySlot(binding.actionId)) {
                            input_v2::telemetry::InputTraceRecorder::GetSingleton().SetActiveSnapshotSequence(
                                snapshot.sequence);
                            input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordKeyboardCommand(
                                backend::KeyboardBridgeCommandType::Pulse,
                                slot->directInputScancode,
                                slot->helperActionId,
                                decision.contract,
                                snapshot.context);
                        }
                    } else if (decision.backend == backend::PlannedBackend::KeyboardHelper) {
                        input_v2::telemetry::InputTraceRecorder::GetSingleton().SetActiveSnapshotSequence(
                            snapshot.sequence);
                        input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordKeyboardCommand(
                            backend::KeyboardBridgeCommandType::Pulse,
                            static_cast<std::uint8_t>(decision.nativeCode),
                            binding.actionId,
                            decision.contract,
                            snapshot.context);
                    }
                    break;
                }
            }
        }
#endif

        const char* ToString(input_v2::presentation::PresentationOwner owner)
        {
            return owner == input_v2::presentation::PresentationOwner::Gamepad ? "Gamepad" : "KeyboardMouse";
        }

        const char* ToString(input_v2::presentation::CursorOwner owner)
        {
            return owner == input_v2::presentation::CursorOwner::Gamepad ? "Gamepad" : "KeyboardMouse";
        }
    }

    PadEventSnapshotProcessor& PadEventSnapshotProcessor::GetSingleton()
    {
        static PadEventSnapshotProcessor instance;
        return instance;
    }

    void PadEventSnapshotProcessor::ResetStateOnOwnerTick()
    {
        if (!input_v2::runtime::RuntimeOwnerGuard::GetSingleton().IsCurrentThreadOwnerTick()) {
            logger::critical("[DualPad][RuntimeOwner] rejected processor reset outside active owner tick");
            return;
        }
        AuthoritativePollState::GetSingleton().Reset();
        backend::NativeButtonCommitBackend::GetSingleton().Reset();
        backend::KeyboardHelperBackend::GetSingleton().Reset();
        input_v2::gameplay::DualPadRuntime::GetSingleton().ResetOnOwnerTick();
        input_v2::ingress::IngressHub::GetSingleton().ResetForTests();
        DirectProcessorAssembler().Reset();
    }

    void PadEventSnapshotProcessor::ResetStateForReplayTests()
    {
        AuthoritativePollState::GetSingleton().Reset();
        backend::NativeButtonCommitBackend::GetSingleton().Reset();
        backend::KeyboardHelperBackend::GetSingleton().Reset();
        input_v2::gameplay::DualPadRuntime::GetSingleton().ResetForTests();
        input_v2::ingress::IngressHub::GetSingleton().ResetForTests();
        DirectProcessorAssembler().Reset();
        input_v2::runtime::RuntimeOwnerGuard::GetSingleton().ResetForTests();
    }

    void PadEventSnapshotProcessor::Process(const PadEventSnapshot& snapshot)
    {
        auto& contextRefresh = input_v2::context::ContextRefreshTick::GetSingleton();
        auto ownerTick = input_v2::runtime::RuntimeOwnerGuard::GetSingleton().TryEnter(
            contextRefresh.BeginFrame());
        if (!ownerTick.Accepted()) {
            return;
        }
        if (snapshot.type == PadEventSnapshotType::Reset) {
            ResetStateOnOwnerTick();
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
        if (!input_v2::runtime::RuntimeOwnerGuard::GetSingleton().IsCurrentThreadOwnerTick()) {
            logger::critical("[DualPad][RuntimeOwner] rejected ProcessIngressFrame outside active owner tick");
            return;
        }
        if (frame.kind == input_v2::ingress::AssembledFrameKind::Transition &&
            frame.transition.requestHardResync) {
            AuthoritativePollState::GetSingleton().Reset();
        }

        const auto result = input_v2::gameplay::DualPadRuntime::GetSingleton().ProcessAssembledFrame(frame);
        input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordRuntimeDebugSnapshot(
            input_v2::gameplay::DualPadRuntime::GetSingleton().GetLastDebugSnapshot());
        if (frame.kind == input_v2::ingress::AssembledFrameKind::Stable && frame.facts.legacySnapshot) {
            const auto& snapshot = *frame.facts.legacySnapshot;
            auto& pollState = AuthoritativePollState::GetSingleton();
#ifdef DUALPAD_REPLAY_HARNESS
            pollState.PublishAnalogState(
                snapshot.state.leftStick.x,
                snapshot.state.leftStick.y,
                snapshot.state.rightStick.x,
                snapshot.state.rightStick.y,
                snapshot.state.leftTrigger.normalized,
                snapshot.state.rightTrigger.normalized);
            RecordReplayCompatHelperCommands(snapshot, result);
#endif
            pollState.PublishFrameMetadata(
                snapshot.sourceTimestampUs,
                snapshot.overflowed,
                snapshot.coalesced);
            pollState.PublishUnmanagedDigitalEdges(0, 0, 0);

            const auto gameplayPresentation =
                input_v2::gameplay::DualPadRuntime::GetSingleton().GetPublishedGameplayPresentation();
            auto gameplayEngineOwner = gameplayPresentation.engineOwner == input_v2::presentation::PresentationOwner::Gamepad ?
                "Gamepad" :
                "KeyboardMouse";
            auto gameplayMenuEntryOwner = gameplayPresentation.menuEntryOwner == input_v2::presentation::PresentationOwner::Gamepad ?
                "Gamepad" :
                "KeyboardMouse";
#ifndef DUALPAD_REPLAY_HARNESS
            const auto& compatibilitySurface = input_v2::presentation::SkyrimCompatibilitySurface::GetSingleton();
            const auto committedPresentation = compatibilitySurface.GetCommittedState();
            const bool isUsingGamepad =
                committedPresentation.owner == input_v2::presentation::PresentationOwner::Gamepad;
            const bool gamepadControlsCursor =
                committedPresentation.cursorOwner == input_v2::presentation::CursorOwner::Gamepad;
            const bool gamepadDeviceEnabled = compatibilitySurface.IsGamepadDeviceEnabledHook(true);
#endif
#ifdef DUALPAD_REPLAY_HARNESS
            const bool replayGamepadActivity =
                snapshot.state.buttons.digitalMask != 0 ||
                snapshot.state.leftStick.x != 0.0f ||
                snapshot.state.leftStick.y != 0.0f ||
                snapshot.state.rightStick.x != 0.0f ||
                snapshot.state.rightStick.y != 0.0f ||
                snapshot.state.leftTrigger.normalized != 0.0f ||
                snapshot.state.rightTrigger.normalized != 0.0f;
            if (replayGamepadActivity) {
                gameplayEngineOwner = "Gamepad";
                gameplayMenuEntryOwner = "Gamepad";
            }
#endif
            input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordProcessedSnapshot(
                snapshot,
                pollState.ReadSnapshot(),
                input_v2::telemetry::ReplayCompatibilitySurface{
                    .context = snapshot.context,
                    .contextEpoch = snapshot.contextEpoch,
#ifdef DUALPAD_REPLAY_HARNESS
                    .isUsingGamepad = false,
                    .gamepadControlsCursor = false,
                    .gamepadDeviceEnabled = false,
                    .presentationOwner = "KeyboardMouse",
                    .cursorOwner = "KeyboardMouse",
#else
                    .isUsingGamepad = isUsingGamepad,
                    .gamepadControlsCursor = gamepadControlsCursor,
                    .gamepadDeviceEnabled = gamepadDeviceEnabled,
                    .presentationOwner = ToString(committedPresentation.owner),
                    .cursorOwner = ToString(committedPresentation.cursorOwner),
#endif
                    .gameplayEngineOwner = gameplayEngineOwner,
                    .gameplayMenuEntryOwner = gameplayMenuEntryOwner
                });
        }
    }
}
