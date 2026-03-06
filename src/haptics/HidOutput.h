#pragma once

#include "haptics/HapticsTypes.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct hid_device_;
using hid_device = struct hid_device_;

namespace dualpad::haptics
{
    struct HapticsConfig;

    class HidOutput
    {
    public:
        static HidOutput& GetSingleton();

        void SetDevice(hid_device* device);
        bool SendFrame(const HidFrame& frame);
        bool SubmitFrameNonBlocking(const HidFrame& frame);
        void FlushPendingOnReaderThread(hid_device* device);
        std::uint32_t GetReaderPollTimeoutMs(std::uint32_t fallbackMs) const;
        void StopVibration();
        bool IsConnected() const;

        struct Stats
        {
            std::uint32_t totalFramesSent{ 0 };
            std::uint32_t totalBytesSent{ 0 };
            std::uint32_t sendFailures{ 0 };
            std::uint32_t sendNoDevice{ 0 };
            std::uint32_t sendWriteFail{ 0 };
            std::uint32_t sendWriteOk{ 0 };

            std::uint32_t txQueuedFg{ 0 };
            std::uint32_t txQueuedBg{ 0 };
            std::uint32_t txDequeuedFg{ 0 };
            std::uint32_t txDequeuedBg{ 0 };
            std::uint32_t txDropQueueFullFg{ 0 };
            std::uint32_t txDropQueueFullBg{ 0 };
            std::uint32_t txDropStaleFg{ 0 };
            std::uint32_t txDropStaleBg{ 0 };
            std::uint32_t txMergedFg{ 0 };
            std::uint32_t txMergedBg{ 0 };
            std::uint32_t txSendOk{ 0 };
            std::uint32_t txSendFail{ 0 };
            std::uint32_t txNoDevice{ 0 };
            std::uint32_t txQueueDepthFg{ 0 };
            std::uint32_t txQueueDepthBg{ 0 };
            std::uint32_t txLatencyP50Us{ 0 };
            std::uint32_t txLatencyP95Us{ 0 };
            std::uint32_t txJitterP95Us{ 0 };
            std::uint32_t txLatencySamples{ 0 };
            std::uint32_t txRenderOverP50Us{ 0 };
            std::uint32_t txRenderOverP95Us{ 0 };
            std::uint32_t txRenderOverSamples{ 0 };
            std::uint32_t txFirstRenderP50Us{ 0 };
            std::uint32_t txFirstRenderP95Us{ 0 };
            std::uint32_t txFirstRenderSamples{ 0 };
            std::uint32_t txSkippedRepeat{ 0 };
            std::uint32_t txStopFlushes{ 0 };
            std::uint32_t txRouteFg{ 0 };
            std::uint32_t txRouteBg{ 0 };
            std::uint32_t txRouteFgZero{ 0 };
            std::uint32_t txRouteFgHint{ 0 };
            std::uint32_t txRouteFgPriority{ 0 };
            std::uint32_t txRouteFgEvent{ 0 };
            std::uint32_t txRouteBgUnknown{ 0 };
            std::uint32_t txRouteBgBackground{ 0 };
            std::uint32_t txSelectForcedFgBudget{ 0 };
            std::uint32_t txSelectBgWhileFgPending{ 0 };
            std::uint32_t txSoftSalvageFg{ 0 };
            std::uint32_t txSoftSalvageBg{ 0 };
            std::uint32_t txSoftSalvageDropped{ 0 };
            std::uint32_t txFlushCapHit{ 0 };
            std::uint32_t txFlushCapDueFg{ 0 };
            std::uint32_t txFlushCapDueBg{ 0 };
            std::uint32_t txFlushNoSelectPending{ 0 };
            std::uint32_t txFlushLookaheadMiss{ 0 };
            std::uint32_t txStateUpdateFg{ 0 };
            std::uint32_t txStateUpdateBg{ 0 };
            std::uint32_t txStateOverwriteFg{ 0 };
            std::uint32_t txStateOverwriteBg{ 0 };
            std::uint32_t txStateCarryQueuedFg{ 0 };
            std::uint32_t txStateCarryQueuedBg{ 0 };
            std::uint32_t txStateCarryDropFg{ 0 };
            std::uint32_t txStateCarryDropBg{ 0 };
            std::uint32_t txStateCarryConsumedFg{ 0 };
            std::uint32_t txStateCarryConsumedBg{ 0 };
            std::uint32_t txStateExpiredDrop{ 0 };
            std::uint32_t txStateFutureSkip{ 0 };
        };

        Stats GetStats() const;

    private:
        struct TrackSlotState
        {
            bool valid{ false };
            bool foreground{ false };
            EventType eventType{ EventType::Unknown };
            SourceType sourceType{ SourceType::AudioMod };
            std::uint8_t priority{ 0 };
            float confidence{ 0.0f };
            bool hint{ false };
            std::uint8_t baseLeft{ 0 };
            std::uint8_t baseRight{ 0 };
            std::uint64_t sourceQpc{ 0 };
            std::uint64_t deadlineUs{ 0 };
            std::uint64_t lastUpdateUs{ 0 };
            std::uint64_t releaseEndUs{ 0 };
            std::uint64_t seq{ 0 };
            bool renderedOnce{ false };
            std::uint64_t epoch{ 0 };
            std::uint64_t supersededAtUs{ 0 };
        };

        struct TrackState
        {
            TrackSlotState active{};
            TrackSlotState pending{};
            std::uint64_t nextReadyUs{ 0 };
            std::uint64_t nextEpoch{ 1 };
            std::uint64_t cadenceLastTokenUs{ 0 };
            std::uint64_t cadenceExpectedGapUs{ 0 };
            std::uint64_t livenessDeadlineUs{ 0 };
            bool stopPending{ false };
            std::uint64_t stopRequestedUs{ 0 };
            std::uint64_t stopSeq{ 0 };
            EventType stopEventType{ EventType::Unknown };
        };

        struct SelectedFrame
        {
            HidFrame frame{};
            bool foreground{ false };
        };

        struct MergeFrameState
        {
            bool initialized{ false };
            float sumLeft{ 0.0f };
            float sumRight{ 0.0f };
            std::uint32_t count{ 0 };
            std::uint8_t maxLeft{ 0 };
            std::uint8_t maxRight{ 0 };
            float maxConfidence{ 0.0f };
            std::uint8_t maxPriority{ 0 };
            EventType dominantEvent{ EventType::Unknown };
            SourceType dominantSource{ SourceType::AudioMod };
            std::uint64_t latestQpc{ 0 };
            std::uint64_t latestTargetQpc{ 0 };
            bool foregroundHint{ false };
        };

        struct OutputStateSlot
        {
            bool valid{ false };
            bool foreground{ false };
            HidFrame frame{};
            std::uint64_t updatedUs{ 0 };
            std::uint64_t expireUs{ 0 };
        };

        HidOutput() = default;
        ~HidOutput() = default;
        HidOutput(const HidOutput&) = delete;
        HidOutput& operator=(const HidOutput&) = delete;

        bool SendVibrateCommand(std::uint8_t leftMotor, std::uint8_t rightMotor);
        bool SendVibrateCommandUnlocked(std::uint8_t leftMotor, std::uint8_t rightMotor);

        bool EnqueueFrame(
            const HidFrame& frame,
            bool foreground,
            std::uint32_t capacity,
            const HapticsConfig& cfg);
        bool TrySelectFrame(
            std::uint64_t nowUs,
            SelectedFrame& outSelected,
            std::uint64_t lookaheadUs,
            bool fgPreempt,
            std::uint32_t bgBudget);
        bool PopDueFront(
            std::deque<HidFrame>& queue,
            std::uint64_t nowUs,
            std::uint64_t lookaheadUs,
            HidFrame& outFrame);
        HidFrame MergeFrames(
            const SelectedFrame& selected,
            std::uint64_t nowUs,
            std::uint64_t mergeWindowUs);
        void AccumulateMerge(MergeFrameState& state, const HidFrame& frame, bool foreground);
        static std::uint32_t PercentileOf(std::vector<std::uint32_t> values, float p);
        void PushLatencySample(std::uint32_t latencyUs, std::uint32_t jitterUs);
        void PushTrackTimingSample(std::uint32_t renderOverUs, std::optional<std::uint32_t> firstRenderUs);
        static bool IsBackgroundEvent(EventType type);
        static bool IsForegroundFrame(const HidFrame& frame, int prioritySwing);
        static std::string WideToUtf8(const wchar_t* wstr);
        static std::uint8_t TrackIndexForFrame(const HidFrame& frame, int prioritySwing);
        static std::uint64_t TrackReleaseWindowUs(EventType type);
        bool SubmitFrameStateTrack(const HidFrame& frame, const HapticsConfig& cfg, std::uint64_t nowUs);
        void FlushStateTrackOnReaderThread(hid_device* device, const HapticsConfig& cfg);
        std::uint32_t GetReaderPollTimeoutMsStateTrack(
            std::uint32_t fallbackMs,
            const HapticsConfig& cfg,
            std::uint64_t nowUs) const;

    private:
        hid_device* _device{ nullptr };

        mutable std::mutex _deviceMutex;
        mutable std::mutex _queueMutex;
        mutable std::mutex _sampleMutex;

        std::atomic<std::uint32_t> _sendNoDevice{ 0 };
        std::atomic<std::uint32_t> _sendWriteFail{ 0 };
        std::atomic<std::uint32_t> _sendWriteOk{ 0 };

        std::atomic<std::uint32_t> _totalFramesSent{ 0 };
        std::atomic<std::uint32_t> _totalBytesSent{ 0 };
        std::atomic<std::uint32_t> _sendFailures{ 0 };
        std::atomic<std::uint32_t> _txQueuedFg{ 0 };
        std::atomic<std::uint32_t> _txQueuedBg{ 0 };
        std::atomic<std::uint32_t> _txDequeuedFg{ 0 };
        std::atomic<std::uint32_t> _txDequeuedBg{ 0 };
        std::atomic<std::uint32_t> _txDropQueueFullFg{ 0 };
        std::atomic<std::uint32_t> _txDropQueueFullBg{ 0 };
        std::atomic<std::uint32_t> _txDropStaleFg{ 0 };
        std::atomic<std::uint32_t> _txDropStaleBg{ 0 };
        std::atomic<std::uint32_t> _txMergedFg{ 0 };
        std::atomic<std::uint32_t> _txMergedBg{ 0 };
        std::atomic<std::uint32_t> _txQueueDepthFg{ 0 };
        std::atomic<std::uint32_t> _txQueueDepthBg{ 0 };
        std::atomic<std::uint32_t> _txSendFail{ 0 };
        std::atomic<std::uint32_t> _txSkippedRepeat{ 0 };
        std::atomic<std::uint32_t> _txStopFlushes{ 0 };
        std::atomic<std::uint32_t> _txRouteFg{ 0 };
        std::atomic<std::uint32_t> _txRouteBg{ 0 };
        std::atomic<std::uint32_t> _txRouteFgZero{ 0 };
        std::atomic<std::uint32_t> _txRouteFgHint{ 0 };
        std::atomic<std::uint32_t> _txRouteFgPriority{ 0 };
        std::atomic<std::uint32_t> _txRouteFgEvent{ 0 };
        std::atomic<std::uint32_t> _txRouteBgUnknown{ 0 };
        std::atomic<std::uint32_t> _txRouteBgBackground{ 0 };
        std::atomic<std::uint32_t> _txSelectForcedFgBudget{ 0 };
        std::atomic<std::uint32_t> _txSelectBgWhileFgPending{ 0 };
        std::atomic<std::uint32_t> _txSoftSalvageFg{ 0 };
        std::atomic<std::uint32_t> _txSoftSalvageBg{ 0 };
        std::atomic<std::uint32_t> _txSoftSalvageDropped{ 0 };
        std::atomic<std::uint32_t> _txFlushCapHit{ 0 };
        std::atomic<std::uint32_t> _txFlushCapDueFg{ 0 };
        std::atomic<std::uint32_t> _txFlushCapDueBg{ 0 };
        std::atomic<std::uint32_t> _txFlushNoSelectPending{ 0 };
        std::atomic<std::uint32_t> _txFlushLookaheadMiss{ 0 };
        std::atomic<std::uint32_t> _txStateUpdateFg{ 0 };
        std::atomic<std::uint32_t> _txStateUpdateBg{ 0 };
        std::atomic<std::uint32_t> _txStateOverwriteFg{ 0 };
        std::atomic<std::uint32_t> _txStateOverwriteBg{ 0 };
        std::atomic<std::uint32_t> _txStateCarryQueuedFg{ 0 };
        std::atomic<std::uint32_t> _txStateCarryQueuedBg{ 0 };
        std::atomic<std::uint32_t> _txStateCarryDropFg{ 0 };
        std::atomic<std::uint32_t> _txStateCarryDropBg{ 0 };
        std::atomic<std::uint32_t> _txStateCarryConsumedFg{ 0 };
        std::atomic<std::uint32_t> _txStateCarryConsumedBg{ 0 };
        std::atomic<std::uint32_t> _txStateExpiredDrop{ 0 };
        std::atomic<std::uint32_t> _txStateFutureSkip{ 0 };

        std::atomic<std::uint64_t> _seqGen{ 1 };
        mutable std::mutex _stateMutex;
        OutputStateSlot _fgState{};
        OutputStateSlot _fgCarryState{};
        OutputStateSlot _bgState{};
        OutputStateSlot _bgCarryState{};
        mutable std::mutex _trackMutex;
        std::array<TrackState, 4> _trackStates{};
        std::atomic<std::uint32_t> _txTrackUpdate{ 0 };
        std::atomic<std::uint32_t> _txTrackDrop{ 0 };
        std::atomic<std::uint32_t> _txTrackSelect{ 0 };
        std::atomic<std::uint32_t> _txTrackRepeatDrop{ 0 };

        std::deque<HidFrame> _fgQueue;
        std::deque<HidFrame> _bgQueue;
        std::uint8_t _lastSubmittedLeft{ 0 };
        std::uint8_t _lastSubmittedRight{ 0 };
        EventType _lastSubmittedEvent{ EventType::Unknown };
        std::uint64_t _lastSubmittedQpc{ 0 };
        bool _lastSubmittedValid{ false };
        std::uint8_t _lastSentLeft{ 0 };
        std::uint8_t _lastSentRight{ 0 };
        EventType _lastSentEvent{ EventType::Unknown };
        bool _lastSentValid{ false };
        std::uint32_t _bgDequeuedSinceFg{ 0 };
        std::uint64_t _lastSendQpc{ 0 };
        std::uint64_t _lastTargetQpc{ 0 };
        std::deque<std::uint32_t> _latencySamplesUs;
        std::deque<std::uint32_t> _jitterSamplesUs;
        std::deque<std::uint32_t> _trackRenderOverSamplesUs;
        std::deque<std::uint32_t> _trackFirstRenderSamplesUs;
    };
}
