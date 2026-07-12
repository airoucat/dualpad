#pragma once

#include "input/state/PadState.h"
#include "input_v2/ingress/InputResetReason.h"
#include "input_v2/ingress/LatestPadState.h"
#include "input_v2/ingress/MeaningfulSourceActivity.h"

#include <cstdint>
#include <vector>

namespace dualpad::input_v2::ingress
{
    enum class GamepadConnectivity : std::uint8_t
    {
        Disconnected = 0,
        Connected
    };

    struct GamepadConnectionDraft
    {
        GamepadConnectivity connectivity{ GamepadConnectivity::Disconnected };
    };

    struct GamepadConnectionFacts
    {
        CausalLatestHeader causal{};
        GamepadConnectivity connectivity{ GamepadConnectivity::Disconnected };
        std::uint64_t gamepadSessionId{ 0 };
    };

    struct GamepadCurrentStateDraft
    {
        std::uint64_t sourceSequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
        dualpad::input::PadState state{};
        std::uint32_t currentDownMask{ 0 };
    };

    enum class GamepadDigitalEdgePhase : std::uint8_t
    {
        Press = 0,
        Release
    };

    struct GamepadDigitalEdgeDraft
    {
        GamepadDigitalEdgePhase phase{ GamepadDigitalEdgePhase::Press };
        std::uint32_t controlCode{ 0 };
        std::uint64_t sourceSequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
    };

    struct GamepadDigitalEdge : GamepadDigitalEdgeDraft
    {
        std::uint64_t ingressSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
    };

    enum class GamepadActivityReason : std::uint8_t
    {
        None = 0,
        DigitalPress,
        LeftStickEntered,
        LeftStickChanged,
        RightStickEntered,
        RightStickChanged,
        LeftTriggerEntered,
        LeftTriggerChanged,
        RightTriggerEntered,
        RightTriggerChanged,
        TouchpadActivity
    };

    struct GamepadActivityDraft
    {
        GamepadActivityReason reason{ GamepadActivityReason::None };
        std::uint32_t controlCode{ 0 };
        float magnitude{ 0.0F };
        std::uint64_t sourceSequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
    };

    struct GamepadMeaningfulActivity : GamepadActivityDraft
    {
        std::uint64_t ingressSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
    };

    struct ClassifiedGamepadReportDraft
    {
        GamepadCurrentStateDraft current{};
        std::vector<GamepadDigitalEdgeDraft> orderedDigitalEdges;
        std::vector<GamepadActivityDraft> meaningfulActivities;
        std::vector<MeaningfulSourceActivityDraft> sourceActivities;
    };

    class GamepadActivityClassifier
    {
    public:
        ClassifiedGamepadReportDraft Classify(
            const dualpad::input::PadState& previous,
            const dualpad::input::PadState& current,
            std::uint64_t sourceSequence,
            std::uint64_t sourceTimestampUs) const;

        void Reset(InputResetReasonMask reasons) noexcept;
    };
}
