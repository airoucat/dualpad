#include "pch.h"
#include "haptics/HapticMixer.h"

#include "haptics/AudioOnlyScorer.h"
#include "haptics/HapticsConfig.h"
#include "haptics/HidOutput.h"
#include "haptics/EngineAudioTap.h"
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

        // NativeOnly 下应当不会进来，这里再防御一次
        if (config.IsNativeOnly()) {
            return;
        }

        // 对 Unknown 事件（纯音频）也允许通过
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

                const auto tap = EngineAudioTap::GetStats();
                const auto aos = AudioOnlyScorer::GetSingleton().GetStats();
                const auto mixer = GetStats();
                const auto hid = HidOutput::GetSingleton().GetStats();
                const auto voice = VoiceManager::GetSingleton().GetStats();

                logger::info(
                    "[Haptics][AudioOnly] SubmitTap(calls={} pushed={} skipCmp={}) "
                    "AudioOnly(pulled={} produced={} lowDrop={}) "
                    "Mixer(active={} frames={} ticks={} src={}) "
                    "HID(frames={} fail={}) Voice(drop={})",
                    tap.submitCalls,
                    tap.submitFeaturesPushed,
                    tap.submitCompressedSkipped,
                    aos.featuresPulled,
                    aos.sourcesProduced,
                    aos.lowEnergyDropped,
                    mixer.activeSources,
                    mixer.framesOutput,
                    mixer.totalTicks,
                    mixer.totalSourcesAdded,
                    hid.totalFramesSent,
                    hid.sendFailures,
                    voice.featuresDropped);
            }
        }

        logger::info("[Haptics][Mixer] Mixer loop stopped");
    }

    HidFrame HapticMixer::ProcessTick()
    {
        _focusManager.Update();

        auto newSources = AudioOnlyScorer::GetSingleton().Update();
        for (auto& source : newSources) {
            AddSource(source);
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