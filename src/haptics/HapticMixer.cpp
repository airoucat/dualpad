#include "pch.h"
#include "haptics/HapticMixer.h"
#include "haptics/DecisionEngine.h"
#include "haptics/AudioOnlyScorer.h"
#include "haptics/DynamicHapticPool.h"
#include "haptics/HapticsConfig.h"
#include "haptics/HidOutput.h"
#include "haptics/EngineAudioTap.h"
#include "haptics/EventNormalizer.h"
#include "haptics/MetricsReporter.h"
#include "haptics/PlayPathHook.h"
#include "haptics/SemanticResolver.h"
#include "haptics/VoiceManager.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <chrono>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr bool kVerboseSourceLogs = false;

        struct PeriodicLogSnapshot
        {
            EngineAudioTap::Stats tap{};
            AudioOnlyScorer::Stats audioOnly{};
            HapticMixer::Stats mixer{};
            DecisionEngine::Stats decision{};
            HidOutput::Stats hid{};
            VoiceManager::Stats voice{};
            PlayPathHook::Stats playPath{};
            EventNormalizer::Stats normalizer{};
            DynamicHapticPool::Stats dynamicPool{};
            MetricsReporter::Snapshot metrics{};
        };

        PeriodicLogSnapshot CollectPeriodicLogSnapshot(HapticMixer& mixer)
        {
            PeriodicLogSnapshot snap{};
            snap.tap = EngineAudioTap::GetStats();
            snap.audioOnly = AudioOnlyScorer::GetSingleton().GetStats();
            snap.mixer = mixer.GetStats();
            snap.decision = DecisionEngine::GetSingleton().GetStats();
            snap.hid = HidOutput::GetSingleton().GetStats();
            snap.voice = VoiceManager::GetSingleton().GetStats();
            snap.playPath = PlayPathHook::GetSingleton().GetStats();
            snap.normalizer = EventNormalizer::GetSingleton().GetStats();
            snap.dynamicPool = DynamicHapticPool::GetSingleton().GetStats();
            snap.metrics = MetricsReporter::GetSingleton().SnapshotAndReset(
                VoiceManager::GetSingleton().GetQueueSize(),
                snap.voice.featuresDropped);
            return snap;
        }

        void LogPeriodicStats(const PeriodicLogSnapshot& s)
        {
            logger::info(
                "[Haptics][AudioOnly] SubmitTap(calls={} pushed={} skipCmp={}) "
                "PlayPath(init={} initRes={} submit={} submitRes={} skipRes={} retry={} skipRL={} skipMax={} scan={} retryRes={} noForm1={} noFormR={} noCtxScan={} noCtxRes={} noCtxNoPtr={} noCtxDeepScan={} noCtxDeepRes={} skipInit={} skipSub={} skipOth={} traceHit={} traceMiss={} upsert={} bindMiss={}) "
                "Normalize(noVoice={} bindMiss={} traceMiss={} traceHit={} patchForm={} patchEvt={}) "
                "AudioOnly(pulled={} produced={} lowDrop={}) "
                "Decision(l1={} l2={} l3={} noCand={} lowFb={} dynHit={} dynMiss={}) "
                "Reason(l2High={} l2Mid={} l2LowPass={}) "
                "Reject(normNoVoice={} normBindMiss={} normTraceMiss={} semNoForm={} semCacheMiss={} semLowConf={} traceUnbound={} traceExpired={} traceDisabled={}) "
                "DynLearn(l2={} noKey={} lowScore={}) "
                "FormSemantic(hit={} miss={} noForm={} cacheMiss={} lowConf={}) "
                "DynPool(size={} admit={} rejKey={} rejLow={} shCall={} shHit={} shMiss={} hit={} miss={} rejMinHit={} rejLowIn={} evict={}) "
                "Metrics(latP50={}us latP95={}us samples={} unknown={:.2f} metaMis={:.2f} qDepth={} drop={}) "
                "Trace(bindHit={}) "
                "Mixer(active={} frames={} ticks={} src={}) "
                "HID(frames={} fail={} ok={} noDev={} writeFail={}) "
                "Voice(drop={}) NoCandSplit(tickNoAudio={} audioNoMatch={})",
                s.tap.submitCalls,
                s.tap.submitFeaturesPushed,
                s.tap.submitCompressedSkipped,
                s.playPath.initCalls,
                s.playPath.initResolved,
                s.playPath.submitCalls,
                s.playPath.submitResolved,
                s.playPath.submitSkipResolved,
                s.playPath.submitRetryScans,
                s.playPath.submitSkipRateLimit,
                s.playPath.submitSkipMaxAttempts,
                s.playPath.submitScanExecuted,
                s.playPath.submitResolvedOnRetry,
                s.playPath.submitNoFormFirstScan,
                s.playPath.submitNoFormRetry,
                s.playPath.submitNoContextScan,
                s.playPath.submitNoContextResolved,
                s.playPath.submitNoContextNoInitPtr,
                s.playPath.submitNoContextDeepScan,
                s.playPath.submitNoContextDeepResolved,
                s.playPath.submitSkipResolvedFromInit,
                s.playPath.submitSkipResolvedFromSubmit,
                s.playPath.submitSkipResolvedOther,
                s.playPath.submitTraceMetaHit,
                s.playPath.submitTraceMetaMiss,
                s.playPath.traceUpserts,
                s.playPath.bindingMisses,
                s.normalizer.noVoiceID,
                s.normalizer.bindingMiss,
                s.normalizer.traceMiss,
                s.normalizer.traceHit,
                s.normalizer.patchedFormID,
                s.normalizer.patchedEventType,
                s.audioOnly.featuresPulled,
                s.audioOnly.sourcesProduced,
                s.audioOnly.lowEnergyDropped,
                s.decision.l1Count,
                s.decision.l2Count,
                s.decision.l3Count,
                s.decision.noCandidate,
                s.decision.lowScoreFallback,
                s.decision.dynamicPoolHit,
                s.decision.dynamicPoolMiss,
                s.decision.l2HighScore,
                s.decision.l2MidScore,
                s.decision.l2LowScorePass,
                s.normalizer.noVoiceID,
                s.normalizer.bindingMiss,
                s.normalizer.traceMiss,
                s.decision.l1FormSemanticNoFormID,
                s.decision.l1FormSemanticCacheMiss,
                s.decision.l1FormSemanticLowConfidence,
                s.decision.traceBindMissUnbound,
                s.decision.traceBindMissExpired,
                s.decision.traceBindBypassDisabled,
                s.decision.dynamicPoolLearnFromL2,
                s.decision.dynamicPoolLearnFromL2NoKey,
                s.decision.dynamicPoolLearnFromL2LowScore,
                s.decision.l1FormSemanticHit,
                s.decision.l1FormSemanticMiss,
                s.decision.l1FormSemanticNoFormID,
                s.decision.l1FormSemanticCacheMiss,
                s.decision.l1FormSemanticLowConfidence,
                s.dynamicPool.currentSize,
                s.dynamicPool.admitted,
                s.dynamicPool.rejectedNoKey,
                s.dynamicPool.rejectedLowConfidence,
                s.dynamicPool.shadowCalls,
                s.dynamicPool.shadowHits,
                s.dynamicPool.shadowMisses,
                s.dynamicPool.resolveHits,
                s.dynamicPool.resolveMisses,
                s.dynamicPool.resolveRejectMinHits,
                s.dynamicPool.resolveRejectLowInput,
                s.dynamicPool.evicted,
                s.metrics.latencyP50Us,
                s.metrics.latencyP95Us,
                s.metrics.sampleCount,
                s.metrics.unknownRatio,
                s.metrics.metaMismatchRatio,
                s.metrics.queueDepth,
                s.metrics.dropCount,
                s.decision.traceBindHit,
                s.mixer.activeSources,
                s.mixer.framesOutput,
                s.mixer.totalTicks,
                s.mixer.totalSourcesAdded,
                s.hid.totalFramesSent,
                s.hid.sendFailures,
                s.hid.sendWriteOk,
                s.hid.sendNoDevice,
                s.hid.sendWriteFail,
                s.voice.featuresDropped,
                s.decision.tickNoAudio,
                s.decision.audioPresentNoMatch);
        }
    }

    HapticMixer& HapticMixer::GetSingleton()
    {
        static HapticMixer instance;
        return instance;
    }

    void HapticMixer::Start()
    {
        if (_running.exchange(true)) {
            logger::warn("[Haptics][Mixer] Already running");
            return;
        }

        logger::info("[Haptics][Mixer] Starting mixer thread...");

        _activeSources.reserve(64);
        _lastLeft = 0.0f;
        _lastRight = 0.0f;

        auto& config = HapticsConfig::GetSingleton();
        _focusManager.SetDuckingRules(config.duckingRules);

        AudioOnlyScorer::GetSingleton().Initialize();
        DecisionEngine::GetSingleton().Initialize();
        EventNormalizer::GetSingleton().ResetStats();
        SemanticResolver::GetSingleton().ResetStats();
        PlayPathHook::GetSingleton().ResetStats();
        DynamicHapticPool::GetSingleton().Clear();
        DynamicHapticPool::GetSingleton().ResetStats();
        MetricsReporter::GetSingleton().Reset();

        _thread = std::jthread([this](std::stop_token st) {
            (void)st;
            MixerThreadLoop();
            });

        logger::info("[Haptics][Mixer] Mixer thread started");
    }

    void HapticMixer::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        logger::info("[Haptics][Mixer] Stopping mixer thread...");

        if (_thread.joinable()) {
            _thread.request_stop();
            _thread.join();
        }

        DecisionEngine::GetSingleton().Shutdown();
        AudioOnlyScorer::GetSingleton().Shutdown();

        {
            std::scoped_lock lock(_mutex);
            _activeSources.clear();
        }

        logger::info("[Haptics][Mixer] Mixer thread stopped");
    }

    void HapticMixer::AddSource(const HapticSourceMsg& msg)
    {
        if (!_running.load(std::memory_order_acquire)) {
            return;
        }

        auto& config = HapticsConfig::GetSingleton();

        if (config.IsNativeOnly()) {
            return;
        }

        if (msg.eventType != EventType::Unknown && !config.IsEventAllowed(msg.eventType)) {
            return;
        }

        ActiveSource source;
        source.msg = msg;

        if (msg.type == SourceType::BaseEvent) {
            source.expireTime = Now() + std::chrono::milliseconds(msg.ttlMs);
        }
        else {
            source.expireTime = FromQPC(msg.qpc) + std::chrono::milliseconds(msg.ttlMs);
        }

        source.currentLeft = msg.left;
        source.currentRight = msg.right;

        {
            std::scoped_lock lock(_mutex);
            auto pos = std::find_if(_activeSources.begin(), _activeSources.end(),
                [&](const ActiveSource& a) { return a.msg.priority < source.msg.priority; });
            _activeSources.insert(pos, std::move(source));
        }

        _totalSourcesAdded.fetch_add(1, std::memory_order_relaxed);

        if constexpr (kVerboseSourceLogs) {
            logger::info("[Haptics][Mixer] Source added: srcType={} eventType={} L={:.3f} R={:.3f} priority={}",
                static_cast<int>(msg.type), ToString(msg.eventType), msg.left, msg.right, msg.priority);
        }
    }

    HapticMixer::Stats HapticMixer::GetStats() const
    {
        Stats s;
        s.totalTicks = _totalTicks.load(std::memory_order_relaxed);
        s.totalSourcesAdded = _totalSourcesAdded.load(std::memory_order_relaxed);
        s.framesOutput = _framesOutput.load(std::memory_order_relaxed);
        s.peakLeft = _peakLeft.load(std::memory_order_relaxed);
        s.peakRight = _peakRight.load(std::memory_order_relaxed);

        {
            std::scoped_lock lock(_mutex);
            s.activeSources = static_cast<std::uint32_t>(_activeSources.size());
        }

        s.avgTickTimeUs = 0.0f;
        return s;
    }

    void HapticMixer::MixerThreadLoop()
    {
        auto& config = HapticsConfig::GetSingleton();
        const auto tickDuration = std::chrono::milliseconds(config.tickMs);

        logger::info("[Haptics][Mixer] Mixer loop started (tick={}ms)", config.tickMs);

        auto nextTick = std::chrono::steady_clock::now();
        auto nextStatsLog = std::chrono::steady_clock::now() + std::chrono::seconds(1);

        while (_running.load(std::memory_order_acquire)) {
            HidFrame frame = ProcessTick();

            HidOutput::GetSingleton().SendFrame(frame);

            _totalTicks.fetch_add(1, std::memory_order_relaxed);
            _framesOutput.fetch_add(1, std::memory_order_relaxed);

            _peakLeft.store(std::max(_peakLeft.load(), frame.leftMotor / 255.0f), std::memory_order_relaxed);
            _peakRight.store(std::max(_peakRight.load(), frame.rightMotor / 255.0f), std::memory_order_relaxed);

            nextTick += tickDuration;
            std::this_thread::sleep_until(nextTick);

            auto now = std::chrono::steady_clock::now();
            if (now > nextTick + tickDuration * 2) {
                nextTick = now;
            }

            if (now >= nextStatsLog) {
                nextStatsLog = now + std::chrono::seconds(1);
                LogPeriodicStats(CollectPeriodicLogSnapshot(*this));
            }
        }

        logger::info("[Haptics][Mixer] Mixer loop stopped");
    }

    HidFrame HapticMixer::ProcessTick()
    {
        _focusManager.Update();

        auto decisions = DecisionEngine::GetSingleton().Update();
        MetricsReporter::GetSingleton().OnDecisions(decisions, ToQPC(Now()));
        for (auto& d : decisions) {
            AddSource(d.source);
        }

        UpdateActiveSources();

        float left = 0.0f;
        float right = 0.0f;
        MixSources(left, right);

        ApplyDucking(left, right);
        ApplyCompressor(left, right);
        ApplyLimiter(left, right);
        ApplySlewLimit(left, right);
        ApplyDeadzone(left, right);

        HidFrame frame{};
        frame.qpc = ToQPC(Now());
        frame.leftMotor = static_cast<std::uint8_t>(std::clamp(left * 255.0f, 0.0f, 255.0f));
        frame.rightMotor = static_cast<std::uint8_t>(std::clamp(right * 255.0f, 0.0f, 255.0f));
        return frame;
    }

    void HapticMixer::UpdateActiveSources()
    {
        auto now = Now();

        std::scoped_lock lock(_mutex);
        auto it = _activeSources.begin();
        while (it != _activeSources.end()) {
            if (now > it->expireTime) {
                it = _activeSources.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void HapticMixer::MixSources(float& outLeft, float& outRight)
    {
        outLeft = 0.0f;
        outRight = 0.0f;

        std::scoped_lock lock(_mutex);
        if (_activeSources.empty()) {
            return;
        }

        float totalWeight = 0.0f;
        for (const auto& src : _activeSources) {
            float duckFactor = 1.0f;

            if (_focusManager.HasFocus()) {
                EventType srcType = src.msg.eventType;
                EventType focusType = _focusManager.GetCurrentFocus();
                if (srcType != focusType) {
                    duckFactor = _focusManager.GetDuckFactorFor(srcType);
                }
            }

            const float weight = src.msg.confidence * duckFactor;
            outLeft += src.currentLeft * weight;
            outRight += src.currentRight * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0f) {
            outLeft /= totalWeight;
            outRight /= totalWeight;
        }
    }

    void HapticMixer::ApplyDucking(float& left, float& right)
    {
        (void)left;
        (void)right;
    }

    void HapticMixer::ApplyCompressor(float& left, float& right)
    {
        constexpr float threshold = 0.7f;
        constexpr float ratio = 3.0f;

        auto compress = [](float x, float thresh, float r) -> float {
            if (x <= thresh) return x;
            float over = x - thresh;
            return thresh + over / r;
            };

        left = compress(left, threshold, ratio);
        right = compress(right, threshold, ratio);
    }

    void HapticMixer::ApplyLimiter(float& left, float& right)
    {
        auto& config = HapticsConfig::GetSingleton();
        left = std::clamp(left, 0.0f, config.limiter);
        right = std::clamp(right, 0.0f, config.limiter);
    }

    void HapticMixer::ApplySlewLimit(float& left, float& right)
    {
        auto& config = HapticsConfig::GetSingleton();

        float maxDelta = config.slewPerTick / 255.0f;

        float deltaLeft = left - _lastLeft;
        float deltaRight = right - _lastRight;

        if (std::abs(deltaLeft) > maxDelta) {
            left = _lastLeft + (deltaLeft > 0 ? maxDelta : -maxDelta);
        }

        if (std::abs(deltaRight) > maxDelta) {
            right = _lastRight + (deltaRight > 0 ? maxDelta : -maxDelta);
        }

        _lastLeft = left;
        _lastRight = right;
    }

    void HapticMixer::ApplyDeadzone(float& left, float& right)
    {
        auto& config = HapticsConfig::GetSingleton();

        if (left < config.deadzone) left = 0.0f;
        if (right < config.deadzone) right = 0.0f;
    }

    float HapticMixer::GetDuckingFactor(SourceType type) const
    {
        (void)type;
        return 1.0f;
    }

    bool HapticMixer::HasHighPrioritySource() const
    {
        auto& config = HapticsConfig::GetSingleton();

        std::scoped_lock lock(_mutex);
        for (const auto& src : _activeSources) {
            if (src.msg.priority >= config.priorityHit) {
                return true;
            }
        }
        return false;
    }

    EventType HapticMixer::GetEventTypeFromSource(const ActiveSource& src) const
    {
        return src.msg.eventType;
    }
}
