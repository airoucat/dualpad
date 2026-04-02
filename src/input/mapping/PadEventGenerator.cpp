#include "pch.h"
#include "input/mapping/PadEventGenerator.h"

#include "input/mapping/EventDebugLogger.h"
#include "input/protocol/DualSenseButtons.h"

#include <format>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::uint32_t kInterestingMenuBits =
            protocol::buttons::kCross |
            protocol::buttons::kCircle |
            protocol::buttons::kTriangle |
            protocol::buttons::kDpadUp |
            protocol::buttons::kDpadDown |
            protocol::buttons::kDpadLeft |
            protocol::buttons::kDpadRight;

        void MaybeLogMenuFrameDelta(
            const PadState& previous,
            const PadState& current,
            InputContext context,
            const PadEventBuffer& events)
        {
            if (!IsMappingDebugLogEnabled()) {
                return;
            }

            const auto previousMask = previous.buttons.digitalMask;
            const auto currentMask = current.buttons.digitalMask;
            const auto pressedMask = currentMask & ~previousMask;
            const auto releasedMask = previousMask & ~currentMask;

            bool sawInterestingEvent = false;
            std::string eventSummary;
            for (std::size_t i = 0; i < events.count; ++i) {
                const auto& event = events[i];
                if ((event.code & kInterestingMenuBits) == 0) {
                    continue;
                }

                sawInterestingEvent = true;
                if (!eventSummary.empty()) {
                    eventSummary += "; ";
                }
                eventSummary += std::format(
                    "{}:0x{:08X}/mods=0x{:08X}",
                    ToString(event.type),
                    event.code,
                    event.modifierMask);
            }

            const bool interestingMask =
                ((previousMask | currentMask | pressedMask | releasedMask) & kInterestingMenuBits) != 0;
            if (!interestingMask && !sawInterestingEvent) {
                return;
            }

            logger::debug(
                "[DualPad][MenuProbe] mapping-frame ctx={} prevSeq={} curSeq={} prevMask=0x{:08X} curMask=0x{:08X} pressed=0x{:08X} released=0x{:08X} eventCount={} events=[{}]",
                ToString(context),
                previous.sequence,
                current.sequence,
                previousMask,
                currentMask,
                pressedMask,
                releasedMask,
                events.count,
                eventSummary);
        }
    }

    void PadEventGenerator::Reset()
    {
        _comboEvaluator.Reset();
        _tapHoldEvaluator.Reset();
        _touchpadMapper.Reset();
    }

    TouchpadMapper& PadEventGenerator::GetTouchpadMapper()
    {
        return _touchpadMapper;
    }

    const TouchpadMapper& PadEventGenerator::GetTouchpadMapper() const
    {
        return _touchpadMapper;
    }

    void PadEventGenerator::Generate(
        const PadState& previous,
        const PadState& current,
        InputContext context,
        PadEventBuffer& outEvents)
    {
        outEvents.Clear();

        _comboEvaluator.Evaluate(previous, current, context, outEvents);
        _axisEvaluator.Evaluate(previous, current, outEvents);
        _tapHoldEvaluator.Evaluate(previous, current, outEvents);
        _layerEvaluator.Evaluate(previous, current, outEvents);
        _touchpadMapper.ProcessTouch(current, outEvents);

        if (outEvents.overflowed) {
            logger::warn(
                "[DualPad][Mapping] Dropped {} pad events because the per-frame buffer is full ({})",
                outEvents.droppedCount,
                kMaxPadEventsPerFrame);
        }

        MaybeLogMenuFrameDelta(previous, current, context, outEvents);
        LogPadEvents(outEvents);
    }
}
