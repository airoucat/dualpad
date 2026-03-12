#include "pch.h"
#include "input/injection/PollStateCommitter.h"

#include "input/RuntimeConfig.h"
#include "input/backend/FrameActionPlan.h"
#include "input/backend/NativeControlCode.h"
#include "input/injection/PadEventSnapshotProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        struct Win32PollCurrentStateApprox
        {
            std::uint32_t packetNumber;
            std::uint16_t buttons;
            std::uint8_t leftTrigger;
            std::uint8_t rightTrigger;
            std::int16_t thumbLX;
            std::int16_t thumbLY;
            std::int16_t thumbRX;
            std::int16_t thumbRY;
        };

        std::uint64_t NowUs()
        {
            using namespace std::chrono;
            return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
        }

        std::uint8_t ToTriggerByte(float value)
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        std::int16_t ToThumbValue(float value)
        {
            const auto clamped = std::clamp(value, -1.0f, 1.0f);
            const auto scale = clamped >= 0.0f ? 32767.0f : 32768.0f;
            return static_cast<std::int16_t>(std::lround(clamped * scale));
        }

        const char* ToString(backend::ButtonCommitPolicy policy)
        {
            switch (policy) {
            case backend::ButtonCommitPolicy::HoldOwner:
                return "HoldOwner";
            case backend::ButtonCommitPolicy::DeferredPulseWhenAllowed:
                return "DeferredPulseWhenAllowed";
            case backend::ButtonCommitPolicy::MinDownWindowPulse:
                return "MinDownWindowPulse";
            case backend::ButtonCommitPolicy::None:
            default:
                return "None";
            }
        }

        void LogPollCommit(
            const backend::VirtualGamepadState& state,
            const backend::DigitalCommitFrame& commitFrame,
            bool wroteCurrentState)
        {
            if (!RuntimeConfig::GetSingleton().LogNativeInjection()) {
                return;
            }

            logger::info(
                "[DualPad][PollCommit] poll={} packet={} prevDown=0x{:08X} nextDown=0x{:08X} rawButtons=0x{:04X} gameplayBroadAllowed={} jumpAllowed={} activateAllowed={} movementAllowed={} sneakingAllowed={} fightingAllowed={} menuControlsAllowed={} wroteCurrentState={}",
                state.pollIndex,
                state.packetNumber,
                commitFrame.previousDownMask,
                commitFrame.nextDownMask,
                state.rawButtons,
                commitFrame.allowance.gameplayBroadAllowed,
                commitFrame.allowance.gameplayJumpingAllowed,
                commitFrame.allowance.gameplayActivateAllowed,
                commitFrame.allowance.gameplayMovementAllowed,
                commitFrame.allowance.gameplaySneakingAllowed,
                commitFrame.allowance.gameplayFightingAllowed,
                commitFrame.allowance.menuControlsAllowed,
                wroteCurrentState);

            for (std::size_t i = 0; i < commitFrame.logEntryCount; ++i) {
                const auto& entry = commitFrame.logEntries[i];
                logger::info(
                    "[DualPad][PollCommit] poll={} control={} policy={} gateClass={} gateAllowed={} prev={} next={} sawPressEdge={} sawReleaseEdge={} visiblePollsRemaining={} deferredPolls={} finalIntentDown={} wroteCurrentState={}",
                    state.pollIndex,
                    backend::ToString(entry.control),
                    ToString(entry.policy),
                    backend::ToString(entry.gateClass),
                    entry.gateAllowed,
                    entry.previousCommittedDown,
                    entry.nextCommittedDown,
                    entry.sawPressEdgeInWindow,
                    entry.sawReleaseEdgeInWindow,
                    entry.visiblePollsRemaining,
                    entry.deferredPolls,
                    entry.finalIntentDown,
                    wroteCurrentState);
            }
        }
    }

    PollStateCommitter& PollStateCommitter::GetSingleton()
    {
        static PollStateCommitter instance;
        return instance;
    }

    bool PollStateCommitter::Commit(void* currentStateBlock)
    {
        if (!currentStateBlock) {
            return false;
        }

        auto& processor = PadEventSnapshotProcessor::GetSingleton();
        const auto& state = processor.CommitNativeStateForPoll(NowUs());
        auto* pollState = reinterpret_cast<Win32PollCurrentStateApprox*>(currentStateBlock);
        pollState->packetNumber = state.packetNumber;
        pollState->buttons = state.rawButtons;
        pollState->leftTrigger = ToTriggerByte(state.leftTrigger);
        pollState->rightTrigger = ToTriggerByte(state.rightTrigger);
        pollState->thumbLX = ToThumbValue(state.moveX);
        pollState->thumbLY = ToThumbValue(state.moveY);
        pollState->thumbRX = ToThumbValue(state.lookX);
        pollState->thumbRY = ToThumbValue(state.lookY);

        LogPollCommit(state, processor.GetLastNativeCommitFrame(), true);
        return true;
    }
}
