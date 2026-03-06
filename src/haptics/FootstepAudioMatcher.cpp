#include "pch.h"
#include "haptics/FootstepAudioMatcher.h"

#include "haptics/FormSemanticCache.h"
#include "haptics/HapticsConfig.h"
#include "haptics/InstanceTraceCache.h"
#include "haptics/VoiceBindingMap.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kMaxProbeLinesPerSecond = 6;
        constexpr float kTimeTauUs = 18000.0f;
        constexpr std::uint64_t kBridgePatchBackUs = 20000ull;
        constexpr std::uint64_t kBridgePatchForwardUs = 140000ull;
        constexpr std::uint64_t kLivePatchTtlUs = 180000ull;

        bool ShouldEmitWindowedProbe(
            std::atomic<std::uint64_t>& windowUs,
            std::atomic<std::uint32_t>& windowLines,
            std::uint64_t tsUs,
            std::uint32_t maxLinesPerSec)
        {
            auto win = windowUs.load(std::memory_order_relaxed);
            if (win == 0 || tsUs < win || (tsUs - win) >= 1000000ull) {
                windowUs.store(tsUs, std::memory_order_relaxed);
                windowLines.store(0, std::memory_order_relaxed);
            }
            return windowLines.fetch_add(1, std::memory_order_relaxed) < maxLinesPerSec;
        }

        float Clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        template <class TValue, class TWeight, class TSelector>
        float WeightedMedianOf(
            const std::vector<TValue>& values,
            const std::vector<TWeight>& weights,
            TSelector selector)
        {
            if (values.empty() || values.size() != weights.size()) {
                return 0.0f;
            }

            std::vector<std::pair<float, float>> ranked{};
            ranked.reserve(values.size());
            float totalWeight = 0.0f;
            for (std::size_t i = 0; i < values.size(); ++i) {
                const float weight = std::max(0.0f, static_cast<float>(weights[i]));
                if (weight <= 0.0f) {
                    continue;
                }
                ranked.emplace_back(selector(values[i]), weight);
                totalWeight += weight;
            }
            if (ranked.empty() || totalWeight <= 0.0f) {
                return 0.0f;
            }

            std::sort(
                ranked.begin(),
                ranked.end(),
                [](const auto& a, const auto& b) {
                    return a.first < b.first;
                });

            float cumulative = 0.0f;
            const float half = totalWeight * 0.5f;
            for (const auto& [value, weight] : ranked) {
                cumulative += weight;
                if (cumulative >= half) {
                    return value;
                }
            }
            return ranked.back().first;
        }

        std::uint64_t ComputeTimeDistanceUs(std::uint64_t anchorUs, const AudioFeatureMsg& feature)
        {
            if (feature.qpcStart == 0 || feature.qpcEnd == 0) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            if (anchorUs < feature.qpcStart) {
                return feature.qpcStart - anchorUs;
            }
            if (anchorUs > feature.qpcEnd) {
                return anchorUs - feature.qpcEnd;
            }
            return 0;
        }

        bool ResolveFootstepMeta(const TraceMeta& trace, float& outConfidence)
        {
            outConfidence = 0.0f;
            if (trace.preferredEvent == EventType::Footstep) {
                outConfidence = std::max(outConfidence, Clamp01(trace.confidence));
            }
            if (trace.semantic == SemanticGroup::Footstep) {
                outConfidence = std::max(outConfidence, Clamp01(trace.confidence));
            }

            const auto formId = (trace.sourceFormId != 0) ? trace.sourceFormId : trace.soundFormId;
            if (formId != 0) {
                FormSemanticMeta meta{};
                if (FormSemanticCache::GetSingleton().TryGet(formId, meta) &&
                    meta.group == SemanticGroup::Footstep) {
                    outConfidence = std::max(outConfidence, Clamp01(meta.confidence));
                }
            }

            return outConfidence > 0.0f;
        }

        std::optional<TraceMeta> ResolveTraceForFeature(const AudioFeatureMsg& feature, std::uint64_t fallbackInstanceId)
        {
            if (feature.instanceId != 0) {
                if (auto trace = InstanceTraceCache::GetSingleton().TryGet(feature.instanceId); trace.has_value()) {
                    return trace;
                }
            }

            if (fallbackInstanceId != 0) {
                if (auto trace = InstanceTraceCache::GetSingleton().TryGet(fallbackInstanceId); trace.has_value()) {
                    return trace;
                }
            }

            return std::nullopt;
        }
    }

    FootstepAudioMatcher& FootstepAudioMatcher::GetSingleton()
    {
        static FootstepAudioMatcher instance;
        return instance;
    }

    void FootstepAudioMatcher::Reset()
    {
        _audioWriteSeq.store(0, std::memory_order_relaxed);
        _featuresObserved.store(0, std::memory_order_relaxed);
        _truthsObserved.store(0, std::memory_order_relaxed);
        _truthsMatched.store(0, std::memory_order_relaxed);
        _livePatchesQueued.store(0, std::memory_order_relaxed);
        _livePatchesApplied.store(0, std::memory_order_relaxed);
        _livePatchesExpired.store(0, std::memory_order_relaxed);
        _truthBridgeBound.store(0, std::memory_order_relaxed);
        _truthBridgeMatched.store(0, std::memory_order_relaxed);
        _truthBridgeNoFeature.store(0, std::memory_order_relaxed);
        _recentMemoryHits.store(0, std::memory_order_relaxed);
        _recentMemoryMisses.store(0, std::memory_order_relaxed);
        _recentMemorySamples.store(0, std::memory_order_relaxed);
        _truthNoWindow.store(0, std::memory_order_relaxed);
        _truthNoSemantic.store(0, std::memory_order_relaxed);
        _truthLowScore.store(0, std::memory_order_relaxed);
        _windowCandidates.store(0, std::memory_order_relaxed);
        _semanticCandidates.store(0, std::memory_order_relaxed);
        _bindingMissCandidates.store(0, std::memory_order_relaxed);
        _traceMissCandidates.store(0, std::memory_order_relaxed);
        {
            std::scoped_lock lock(_pendingMutex);
            _pendingTruths.clear();
            _pendingPatches.clear();
            _readyLivePatches.clear();
            _recentModifierSamples.clear();
            _lastTruthUsByBucket.fill(0);
        }
        {
            std::scoped_lock lock(_sampleMutex);
            _matchDeltasUs.clear();
            _matchDurationsUs.clear();
            _matchScoresPermille.clear();
            _matchPanAbsPermille.clear();
        }
    }

    void FootstepAudioMatcher::ObserveAudioFeature(const AudioFeatureMsg& msg)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioMatcherShadow) {
            return;
        }

        const auto writeSeq = _audioWriteSeq.fetch_add(1, std::memory_order_acq_rel) + 1ull;
        auto& slot = _audioRing[writeSeq % kAudioRingCapacity];
        const auto writingVersion = writeSeq * 2ull - 1ull;
        const auto stableVersion = writeSeq * 2ull;

        slot.version.store(writingVersion, std::memory_order_release);
        slot.feature = msg;
        slot.version.store(stableVersion, std::memory_order_release);
        _featuresObserved.fetch_add(1, std::memory_order_relaxed);
    }

    void FootstepAudioMatcher::ObserveTruthEvent(std::uint64_t truthUs, std::string_view tag)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioMatcherShadow) {
            return;
        }

        _truthsObserved.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(_pendingMutex);
        const auto bucket = ClassifyMemoryBucket(tag);
        const auto bucketIndex = static_cast<std::size_t>(bucket);
        std::uint32_t truthGapUs = 0;
        if (bucket != MemoryBucket::Unknown && bucketIndex < _lastTruthUsByBucket.size()) {
            const auto prevTruthUs = _lastTruthUsByBucket[bucketIndex];
            if (prevTruthUs != 0 && truthUs > prevTruthUs) {
                truthGapUs = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(truthUs - prevTruthUs, std::numeric_limits<std::uint32_t>::max()));
            }
            _lastTruthUsByBucket[bucketIndex] = truthUs;
        }
        _truthHints.push_back(TruthHint{
            .truthUs = truthUs,
            .bucket = bucket,
            .truthGapUs = truthGapUs
        });
        while (_truthHints.size() > 48) {
            _truthHints.pop_front();
        }
        _pendingTruths.push_back(PendingTruth{
            .truthUs = truthUs,
            .tag = std::string(tag)
        });
    }

    bool FootstepAudioMatcher::PrimeRecentMemoryPatchForTruth(std::uint64_t truthUs, std::uint64_t nowUs)
    {
        return PrimeRecentMemoryPatchForTruth(truthUs, std::string_view{}, nowUs);
    }

    FootstepAudioMatcher::MemoryBucket FootstepAudioMatcher::ClassifyMemoryBucket(std::string_view tag)
    {
        if (tag.find("FootLeft") != std::string_view::npos || tag.find("Left") != std::string_view::npos) {
            return MemoryBucket::FootLeft;
        }
        if (tag.find("FootRight") != std::string_view::npos || tag.find("Right") != std::string_view::npos) {
            return MemoryBucket::FootRight;
        }
        if (tag.find("JumpUp") != std::string_view::npos) {
            return MemoryBucket::JumpUp;
        }
        if (tag.find("JumpDown") != std::string_view::npos) {
            return MemoryBucket::JumpDown;
        }
        return MemoryBucket::Unknown;
    }

    bool FootstepAudioMatcher::IsWalkBucket(MemoryBucket bucket)
    {
        return bucket == MemoryBucket::FootLeft || bucket == MemoryBucket::FootRight;
    }

    FootstepAudioMatcher::MemoryBucket FootstepAudioMatcher::OppositeWalkBucket(MemoryBucket bucket)
    {
        if (bucket == MemoryBucket::FootLeft) {
            return MemoryBucket::FootRight;
        }
        if (bucket == MemoryBucket::FootRight) {
            return MemoryBucket::FootLeft;
        }
        return MemoryBucket::Unknown;
    }

    bool FootstepAudioMatcher::PrimeRecentMemoryPatchForTruth(
        std::uint64_t truthUs,
        std::string_view tag,
        std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepRecentModifierMemory ||
            !cfg.enableFootstepAudioModifierLivePatch ||
            truthUs == 0) {
            return false;
        }
        if (nowUs == 0) {
            nowUs = ToQPC(Now());
        }

        std::scoped_lock lock(_pendingMutex);
        const auto bucket = ClassifyMemoryBucket(tag);
        std::uint32_t currentGapUs = 0;
        for (auto it = _truthHints.rbegin(); it != _truthHints.rend(); ++it) {
            if (it->truthUs == truthUs) {
                currentGapUs = it->truthGapUs;
                break;
            }
        }

        ExpireRecentModifierLocked(nowUs);
        auto live = BuildRecentMemoryPatchLocked(truthUs, bucket, currentGapUs, nowUs);
        if (!live.has_value()) {
            _recentMemoryMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        while (!_readyLivePatches.empty() && _readyLivePatches.front().expireUs <= nowUs) {
            _readyLivePatches.pop_front();
            _livePatchesExpired.fetch_add(1, std::memory_order_relaxed);
        }

        for (auto it = _readyLivePatches.begin(); it != _readyLivePatches.end(); ++it) {
            if (it->truthUs != truthUs) {
                continue;
            }
            if (it->fromRecentMemory) {
                *it = *live;
                return true;
            }
            return false;
        }

        _readyLivePatches.push_back(*live);
        _livePatchesQueued.fetch_add(1, std::memory_order_relaxed);
        _recentMemoryHits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void FootstepAudioMatcher::Tick(std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioMatcherShadow) {
            return;
        }

        if (nowUs == 0) {
            nowUs = ToQPC(Now());
        }

        ProcessMatureTruths(nowUs);
    }

    FootstepAudioMatcher::Stats FootstepAudioMatcher::GetStats()
    {
        Tick(ToQPC(Now()));

        Stats s;
        s.featuresObserved = _featuresObserved.load(std::memory_order_relaxed);
        s.truthsObserved = _truthsObserved.load(std::memory_order_relaxed);
        s.truthsMatched = _truthsMatched.load(std::memory_order_relaxed);
        s.livePatchesQueued = _livePatchesQueued.load(std::memory_order_relaxed);
        s.livePatchesApplied = _livePatchesApplied.load(std::memory_order_relaxed);
        s.livePatchesExpired = _livePatchesExpired.load(std::memory_order_relaxed);
        s.truthBridgeBound = _truthBridgeBound.load(std::memory_order_relaxed);
        s.truthBridgeMatched = _truthBridgeMatched.load(std::memory_order_relaxed);
        s.truthBridgeNoFeature = _truthBridgeNoFeature.load(std::memory_order_relaxed);
        s.recentMemoryHits = _recentMemoryHits.load(std::memory_order_relaxed);
        s.recentMemoryMisses = _recentMemoryMisses.load(std::memory_order_relaxed);
        s.recentMemorySamples = _recentMemorySamples.load(std::memory_order_relaxed);
        s.truthNoWindow = _truthNoWindow.load(std::memory_order_relaxed);
        s.truthNoSemantic = _truthNoSemantic.load(std::memory_order_relaxed);
        s.truthLowScore = _truthLowScore.load(std::memory_order_relaxed);
        s.windowCandidates = _windowCandidates.load(std::memory_order_relaxed);
        s.semanticCandidates = _semanticCandidates.load(std::memory_order_relaxed);
        s.bindingMissCandidates = _bindingMissCandidates.load(std::memory_order_relaxed);
        s.traceMissCandidates = _traceMissCandidates.load(std::memory_order_relaxed);
        {
            std::scoped_lock lock(_sampleMutex);
            s.matchDeltaP50Us = PercentileOf(_matchDeltasUs, 0.50f);
            s.matchDeltaP95Us = PercentileOf(_matchDeltasUs, 0.95f);
            s.matchDurationP50Us = PercentileOf(_matchDurationsUs, 0.50f);
            s.matchDurationP95Us = PercentileOf(_matchDurationsUs, 0.95f);
            s.matchScoreP50Permille = PercentileOf(_matchScoresPermille, 0.50f);
            s.matchScoreP95Permille = PercentileOf(_matchScoresPermille, 0.95f);
            s.matchPanAbsP50Permille = PercentileOf(_matchPanAbsPermille, 0.50f);
            s.matchPanAbsP95Permille = PercentileOf(_matchPanAbsPermille, 0.95f);
            s.samples = static_cast<std::uint32_t>(_matchDeltasUs.size());
        }
        {
            std::scoped_lock lock(_pendingMutex);
            s.pendingTruths = static_cast<std::uint32_t>(
                _pendingTruths.size() + _pendingPatches.size() + _readyLivePatches.size());
        }
        return s;
    }

    std::optional<FootstepAudioMatcher::LivePatch> FootstepAudioMatcher::TryConsumeLivePatchForTruth(
        std::uint64_t truthUs,
        std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioMatcherShadow ||
            !cfg.enableFootstepAudioModifierLivePatch ||
            truthUs == 0) {
            return std::nullopt;
        }
        if (nowUs == 0) {
            nowUs = ToQPC(Now());
        }

        std::scoped_lock lock(_pendingMutex);
        while (!_readyLivePatches.empty() && _readyLivePatches.front().expireUs <= nowUs) {
            _readyLivePatches.pop_front();
            _livePatchesExpired.fetch_add(1, std::memory_order_relaxed);
        }
        ExpireRecentModifierLocked(nowUs);

        for (auto it = _readyLivePatches.begin(); it != _readyLivePatches.end(); ++it) {
            if (it->truthUs != truthUs) {
                continue;
            }

            auto patch = *it;
            _readyLivePatches.erase(it);
            _livePatchesApplied.fetch_add(1, std::memory_order_relaxed);
            RememberRecentModifierLocked(patch, nowUs);
            return patch;
        }

        return std::nullopt;
    }

    std::optional<FootstepAudioMatcher::LivePatch> FootstepAudioMatcher::BuildRecentMemoryPatchLocked(
        std::uint64_t truthUs,
        MemoryBucket bucket,
        std::uint32_t truthGapUs,
        std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (_recentModifierSamples.empty()) {
            return std::nullopt;
        }

        std::vector<RecentModifierSample> selected{};
        std::vector<float> weights{};
        selected.reserve(_recentModifierSamples.size());
        weights.reserve(_recentModifierSamples.size());

        for (const auto& sample : _recentModifierSamples) {
            float familyWeight = 0.0f;
            float panSign = 1.0f;

            if (bucket == MemoryBucket::Unknown || sample.bucket == bucket) {
                familyWeight = 1.0f;
            } else if (IsWalkBucket(bucket) && IsWalkBucket(sample.bucket)) {
                familyWeight = 0.45f;
                if (sample.bucket != bucket) {
                    panSign = -1.0f;
                }
            } else {
                continue;
            }

            const auto ageUs = (nowUs > sample.observedUs) ? (nowUs - sample.observedUs) : 0ull;
            const float freshness = std::clamp(
                1.0f - static_cast<float>(ageUs) /
                    static_cast<float>(cfg.footstepRecentModifierMemoryMaxAgeUs),
                0.15f,
                1.0f);
            const float confidence = std::clamp(
                static_cast<float>(sample.scorePermille) / 1000.0f,
                0.55f,
                1.0f);
            float cadenceWeight = 0.80f;
            if (truthGapUs != 0 && sample.truthGapUs != 0) {
                const auto maxGap = static_cast<float>(std::max(truthGapUs, sample.truthGapUs));
                const auto minGap = static_cast<float>(std::min(truthGapUs, sample.truthGapUs));
                cadenceWeight = std::clamp(minGap / std::max(1.0f, maxGap), 0.45f, 1.0f);
            }

            auto adjusted = sample;
            adjusted.panSigned *= panSign;
            selected.push_back(adjusted);
            weights.push_back(freshness * confidence * familyWeight * cadenceWeight);
        }
        if (selected.empty()) {
            return std::nullopt;
        }

        const float medianAmp = WeightedMedianOf(
            selected,
            weights,
            [](const RecentModifierSample& sample) {
                return sample.ampScale;
            });
        const float medianTargetEnd = WeightedMedianOf(
            selected,
            weights,
            [](const RecentModifierSample& sample) {
                return static_cast<float>(sample.targetEndDeltaUs);
            });

        std::vector<std::size_t> order(selected.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(
            order.begin(),
            order.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                return weights[lhs] > weights[rhs];
            });

        const auto keepCount = std::max<std::size_t>(1, (order.size() * 2 + 2) / 3);
        float trimmedPan = 0.0f;
        float trimmedScore = 0.0f;
        float trimmedWeight = 0.0f;
        for (std::size_t i = 0; i < keepCount; ++i) {
            const auto idx = order[i];
            trimmedPan += selected[idx].panSigned * weights[idx];
            trimmedScore += static_cast<float>(selected[idx].scorePermille) * weights[idx];
            trimmedWeight += weights[idx];
        }
        if (trimmedWeight <= 0.0f) {
            return std::nullopt;
        }

        LivePatch live{};
        live.truthUs = truthUs;
        live.expireUs = nowUs + kLivePatchTtlUs;
        live.ampScale = std::clamp(1.0f + (medianAmp - 1.0f) * 0.80f, 0.92f, 1.15f);
        live.panSigned = std::clamp((trimmedPan / trimmedWeight) * 0.78f, -0.40f, 0.40f);
        live.targetEndUs = truthUs + std::clamp<std::uint64_t>(
            static_cast<std::uint64_t>(std::max(0.0f, medianTargetEnd)),
            42000ull,
            static_cast<std::uint64_t>(cfg.stateTrackFootstepPatchLeaseUs));
        live.patchLeaseUs = cfg.stateTrackFootstepPatchLeaseUs;
        live.scorePermille = static_cast<std::uint16_t>(std::clamp(
            trimmedScore / trimmedWeight,
            550.0f,
            1000.0f));
        live.fromRecentMemory = true;
        live.bucket = bucket;
        live.truthGapUs = truthGapUs;
        return live;
    }

    void FootstepAudioMatcher::ProcessMatureTruths(std::uint64_t nowUs)
    {
        PromoteTruthsToPatches(nowUs);
        ResolvePendingPatches(nowUs);
    }

    void FootstepAudioMatcher::PromoteTruthsToPatches(std::uint64_t nowUs)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        const auto& cfg = HapticsConfig::GetSingleton();
        const auto bridgeSettleUs = std::max<std::uint64_t>(
            100000ull,
            static_cast<std::uint64_t>(cfg.footstepAudioMatcherLookaheadUs) + 40000ull);
        const auto patchWaitUs = std::max<std::uint64_t>(
            120000ull,
            static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookbackUs) + 60000ull);

        std::deque<PendingTruth> fallbackTruths{};
        {
            std::scoped_lock lock(_pendingMutex);
            std::deque<PendingTruth> keepTruths{};
            while (!_pendingTruths.empty()) {
                auto truth = std::move(_pendingTruths.front());
                _pendingTruths.pop_front();

                const auto binding =
                    cfg.enableFootstepTruthBridgeShadow ?
                    FootstepTruthBridge::GetSingleton().TryGetBindingForTruth(truth.truthUs, truth.tag) :
                    std::nullopt;
                if (binding.has_value()) {
                    // 中文说明：桥接成功后，不再回扫挑“最佳样本”，只等待同因果的音频 patch。
                    _pendingPatches.push_back(PendingPatch{
                        .truthUs = truth.truthUs,
                        .tag = std::move(truth.tag),
                        .binding = *binding,
                        .expireUs = binding->observedUs + patchWaitUs
                    });
                    continue;
                }

                if ((truth.truthUs + bridgeSettleUs) <= nowUs) {
                    fallbackTruths.push_back(std::move(truth));
                    continue;
                }

                keepTruths.push_back(std::move(truth));
            }
            _pendingTruths.swap(keepTruths);
        }

        for (const auto& truth : fallbackTruths) {
            const auto match = MatchFallbackTruth(truth.truthUs);
            _windowCandidates.fetch_add(match.windowCandidates, std::memory_order_relaxed);
            _semanticCandidates.fetch_add(match.semanticCandidates, std::memory_order_relaxed);
            _bindingMissCandidates.fetch_add(match.bindingMissCandidates, std::memory_order_relaxed);
            _traceMissCandidates.fetch_add(match.traceMissCandidates, std::memory_order_relaxed);

            if (match.matched) {
                _truthsMatched.fetch_add(1, std::memory_order_relaxed);
                CommitMatchSamples(match);
                if ((match.matchDeltaUs >= 20000u || match.matchScorePermille < 650u) &&
                    ShouldEmitWindowedProbe(s_probeWindowUs, s_probeLines, truth.truthUs, kMaxProbeLinesPerSecond)) {
                    logger::info(
                        "[Haptics][FootAudio] shadow_weak tag={} delta={}us score={:.2f} dur={}us panAbs={:.2f} windowCand={} semCand={}",
                        truth.tag,
                        match.matchDeltaUs,
                        static_cast<float>(match.matchScorePermille) / 1000.0f,
                        match.matchDurationUs,
                        static_cast<float>(match.matchPanAbsPermille) / 1000.0f,
                        match.windowCandidates,
                        match.semanticCandidates);
                }
                continue;
            }

            const char* reason = "low_score";
            if (match.windowCandidates == 0) {
                _truthNoWindow.fetch_add(1, std::memory_order_relaxed);
                reason = "no_window";
            } else if (match.semanticCandidates == 0) {
                _truthNoSemantic.fetch_add(1, std::memory_order_relaxed);
                reason = "no_semantic";
            } else {
                _truthLowScore.fetch_add(1, std::memory_order_relaxed);
            }

            if (ShouldEmitWindowedProbe(s_probeWindowUs, s_probeLines, truth.truthUs, kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][FootAudio] shadow_miss tag={} reason={} bestScore={:.2f} bestDelta={}us windowCand={} semCand={} bindMiss={} traceMiss={}",
                    truth.tag,
                    reason,
                    match.bestScore,
                    match.bestDeltaUs,
                    match.windowCandidates,
                    match.semanticCandidates,
                    match.bindingMissCandidates,
                    match.traceMissCandidates);
            }
        }
    }

    void FootstepAudioMatcher::ResolvePendingPatches(std::uint64_t nowUs)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        std::deque<PendingPatch> patches{};
        {
            std::scoped_lock lock(_pendingMutex);
            patches.swap(_pendingPatches);
        }

        std::deque<PendingPatch> keepPatches{};
        for (const auto& patch : patches) {
            auto match = TryResolvePatch(patch);
            _windowCandidates.fetch_add(match.windowCandidates, std::memory_order_relaxed);
            _semanticCandidates.fetch_add(match.semanticCandidates, std::memory_order_relaxed);
            _bindingMissCandidates.fetch_add(match.bindingMissCandidates, std::memory_order_relaxed);
            _traceMissCandidates.fetch_add(match.traceMissCandidates, std::memory_order_relaxed);

            if (match.matched) {
                _truthBridgeBound.fetch_add(1, std::memory_order_relaxed);
                _truthBridgeMatched.fetch_add(1, std::memory_order_relaxed);
                _truthsMatched.fetch_add(1, std::memory_order_relaxed);
                QueueLivePatch(patch, match, nowUs);
                CommitMatchSamples(match);
                if ((match.matchDeltaUs >= 20000u || match.matchScorePermille < 650u) &&
                    ShouldEmitWindowedProbe(s_probeWindowUs, s_probeLines, patch.truthUs, kMaxProbeLinesPerSecond)) {
                    logger::info(
                        "[Haptics][FootAudio] shadow_weak tag={} delta={}us score={:.2f} dur={}us panAbs={:.2f} windowCand={} semCand={}",
                        patch.tag,
                        match.matchDeltaUs,
                        static_cast<float>(match.matchScorePermille) / 1000.0f,
                        match.matchDurationUs,
                        static_cast<float>(match.matchPanAbsPermille) / 1000.0f,
                        match.windowCandidates,
                        match.semanticCandidates);
                }
                continue;
            }

            if (nowUs < patch.expireUs) {
                keepPatches.push_back(patch);
                continue;
            }

            _truthBridgeBound.fetch_add(1, std::memory_order_relaxed);
            if (match.bridgeNoFeature) {
                _truthBridgeNoFeature.fetch_add(1, std::memory_order_relaxed);
            }

            auto fallback = MatchFallbackTruth(patch.truthUs);
            _windowCandidates.fetch_add(fallback.windowCandidates, std::memory_order_relaxed);
            _semanticCandidates.fetch_add(fallback.semanticCandidates, std::memory_order_relaxed);
            _bindingMissCandidates.fetch_add(fallback.bindingMissCandidates, std::memory_order_relaxed);
            _traceMissCandidates.fetch_add(fallback.traceMissCandidates, std::memory_order_relaxed);

            if (fallback.matched) {
                fallback.bridgeBound = true;
                _truthsMatched.fetch_add(1, std::memory_order_relaxed);
                CommitMatchSamples(fallback);
                if ((fallback.matchDeltaUs >= 20000u || fallback.matchScorePermille < 650u) &&
                    ShouldEmitWindowedProbe(s_probeWindowUs, s_probeLines, patch.truthUs, kMaxProbeLinesPerSecond)) {
                    logger::info(
                        "[Haptics][FootAudio] shadow_weak tag={} delta={}us score={:.2f} dur={}us panAbs={:.2f} windowCand={} semCand={}",
                        patch.tag,
                        fallback.matchDeltaUs,
                        static_cast<float>(fallback.matchScorePermille) / 1000.0f,
                        fallback.matchDurationUs,
                        static_cast<float>(fallback.matchPanAbsPermille) / 1000.0f,
                        fallback.windowCandidates,
                        fallback.semanticCandidates);
                }
                continue;
            }

            const char* reason = "low_score";
            if (match.bridgeNoFeature) {
                reason = "bridge_no_feature";
            } else if (fallback.windowCandidates == 0) {
                _truthNoWindow.fetch_add(1, std::memory_order_relaxed);
                reason = "no_window";
            } else if (fallback.semanticCandidates == 0) {
                _truthNoSemantic.fetch_add(1, std::memory_order_relaxed);
                reason = "no_semantic";
            } else {
                _truthLowScore.fetch_add(1, std::memory_order_relaxed);
            }

            if (ShouldEmitWindowedProbe(s_probeWindowUs, s_probeLines, patch.truthUs, kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][FootAudio] shadow_miss tag={} reason={} bestScore={:.2f} bestDelta={}us windowCand={} semCand={} bindMiss={} traceMiss={}",
                    patch.tag,
                    reason,
                    std::max(match.bestScore, fallback.bestScore),
                    std::min(match.bestDeltaUs, fallback.bestDeltaUs),
                    match.windowCandidates + fallback.windowCandidates,
                    match.semanticCandidates + fallback.semanticCandidates,
                    match.bindingMissCandidates + fallback.bindingMissCandidates,
                    match.traceMissCandidates + fallback.traceMissCandidates);
            }
        }

        if (!keepPatches.empty()) {
            std::scoped_lock lock(_pendingMutex);
            for (auto& patch : keepPatches) {
                _pendingPatches.push_back(std::move(patch));
            }
        }
    }

    FootstepAudioMatcher::MatchResult FootstepAudioMatcher::TryResolvePatch(const PendingPatch& patch) const
    {
        MatchResult out{};
        out.bridgeBound = true;

        const auto latestSeq = _audioWriteSeq.load(std::memory_order_acquire);
        if (latestSeq == 0) {
            out.bridgeNoFeature = true;
            out.bestDeltaUs = std::numeric_limits<std::uint64_t>::max();
            return out;
        }

        const auto scanBudget = std::min<std::uint64_t>(latestSeq, static_cast<std::uint64_t>(kAudioRingCapacity));
        const auto freshBackUs = std::min<std::uint64_t>(
            std::max<std::uint64_t>(5000ull, HapticsConfig::GetSingleton().footstepAudioMatcherLookbackUs),
            kBridgePatchBackUs);
        const auto freshForwardUs = std::max<std::uint64_t>(
            kBridgePatchForwardUs,
            static_cast<std::uint64_t>(HapticsConfig::GetSingleton().footstepAudioMatcherLookaheadUs));

        std::uint64_t bestObservedDeltaUs = std::numeric_limits<std::uint64_t>::max();
        bool haveBest = false;
        bool sawIdentityFeature = false;

        auto scanIdentity = [&](bool exactInstance) {
            for (std::uint64_t i = 0; i < scanBudget; ++i) {
                const auto seq = latestSeq - i;
                if (seq == 0) {
                    break;
                }

                const auto expectedVersion = seq * 2ull;
                const auto& slot = _audioRing[seq % kAudioRingCapacity];
                const auto v1 = slot.version.load(std::memory_order_acquire);
                if (v1 != expectedVersion) {
                    continue;
                }

                const auto feature = slot.feature;
                const auto v2 = slot.version.load(std::memory_order_acquire);
                if (v2 != expectedVersion) {
                    continue;
                }

                if (feature.qpcStart == 0 || feature.qpcEnd == 0 || feature.voiceId == 0) {
                    continue;
                }
                if (feature.qpcStart > (patch.binding.observedUs + freshForwardUs)) {
                    continue;
                }
                if ((feature.qpcEnd + freshBackUs) < patch.binding.observedUs) {
                    break;
                }

                const bool identityMatch = exactInstance ?
                    (patch.binding.instanceId != 0 && feature.instanceId == patch.binding.instanceId) :
                    (patch.binding.voicePtr != 0 &&
                        patch.binding.generation != 0 &&
                        feature.voiceId == static_cast<std::uint64_t>(patch.binding.voicePtr) &&
                        feature.voiceGeneration == patch.binding.generation);
                if (!identityMatch) {
                    continue;
                }

                sawIdentityFeature = true;
                ++out.windowCandidates;

                const auto trace = ResolveTraceForFeature(feature, patch.binding.instanceId);
                if (!trace.has_value()) {
                    ++out.traceMissCandidates;
                    continue;
                }

                float metaConfidence = 0.0f;
                if (!ResolveFootstepMeta(*trace, metaConfidence)) {
                    continue;
                }

                ++out.semanticCandidates;

                const auto deltaTruthUs = ComputeTimeDistanceUs(patch.truthUs, feature);
                const auto deltaObservedUs = ComputeTimeDistanceUs(patch.binding.observedUs, feature);
                const auto durationUs = static_cast<std::uint32_t>(
                    std::clamp<std::uint64_t>(
                        (feature.qpcEnd > feature.qpcStart) ? (feature.qpcEnd - feature.qpcStart) : 0ull,
                        0ull,
                        std::numeric_limits<std::uint32_t>::max()));
                const auto sum = std::max(1e-4f, feature.energyL + feature.energyR);
                const auto pan = std::clamp((feature.energyR - feature.energyL) / sum, -1.0f, 1.0f);
                const auto panAbs = std::abs(pan);
                const auto freshness = Clamp01(std::exp(-static_cast<float>(deltaObservedUs) / 40000.0f));
                const auto energyScore = Clamp01(0.65f * feature.peak + 0.35f * feature.rms);
                const auto score = Clamp01(
                    0.55f * Clamp01(metaConfidence) +
                    0.25f * freshness +
                    0.20f * energyScore);

                const bool better =
                    !haveBest ||
                    deltaObservedUs < bestObservedDeltaUs ||
                    (deltaObservedUs == bestObservedDeltaUs && score > out.bestScore) ||
                    (deltaObservedUs == bestObservedDeltaUs && score == out.bestScore && deltaTruthUs < out.bestDeltaUs);
                if (!better) {
                    continue;
                }

                haveBest = true;
                bestObservedDeltaUs = deltaObservedUs;
                out.bestScore = score;
                out.bestDeltaUs = deltaTruthUs;
                if (score >= HapticsConfig::GetSingleton().footstepAudioMatcherMinScore) {
                    out.matched = true;
                    out.bridgeMatched = true;
                    out.matchDeltaUs = static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(deltaTruthUs, std::numeric_limits<std::uint32_t>::max()));
                    out.matchDurationUs = durationUs;
                    out.matchScorePermille = static_cast<std::uint16_t>(
                        std::clamp(score * 1000.0f, 0.0f, 1000.0f));
                    out.matchPanAbsPermille = static_cast<std::uint16_t>(
                        std::clamp(panAbs * 1000.0f, 0.0f, 1000.0f));
                    out.matchPanSigned = pan;
                    out.matchEnergyScore = energyScore;
                }
            }
        };

        // 中文说明：patch 模式优先 exact instance；只有 exact instance 缺席时才退到 voice+generation。
        scanIdentity(true);
        if (!haveBest) {
            scanIdentity(false);
        }

        if (!sawIdentityFeature) {
            out.bridgeNoFeature = true;
            out.bestDeltaUs = std::numeric_limits<std::uint64_t>::max();
        }
        return out;
    }

    FootstepAudioMatcher::MatchResult FootstepAudioMatcher::MatchFallbackTruth(std::uint64_t truthUs) const
    {
        MatchResult out{};
        bool haveBest = false;
        const auto& cfg = HapticsConfig::GetSingleton();
        const auto latestSeq = _audioWriteSeq.load(std::memory_order_acquire);
        if (latestSeq == 0) {
            out.bestDeltaUs = std::numeric_limits<std::uint64_t>::max();
            return out;
        }

        const auto lookbackUs = static_cast<std::uint64_t>(cfg.footstepAudioMatcherLookbackUs);
        const auto lookaheadUs = static_cast<std::uint64_t>(cfg.footstepAudioMatcherLookaheadUs);
        const auto ttlUs = static_cast<std::uint64_t>(
            std::max<std::uint32_t>(100u, cfg.traceBindingTtlMs) * 1000ull);
        const auto scanBudget = std::min<std::uint64_t>({
            latestSeq,
            static_cast<std::uint64_t>(kAudioRingCapacity),
            static_cast<std::uint64_t>(std::max<std::uint32_t>(1u, cfg.footstepAudioMatcherMaxCandidates))
        });

        for (std::uint64_t i = 0; i < scanBudget; ++i) {
            const auto seq = latestSeq - i;
            if (seq == 0) {
                break;
            }

            const auto expectedVersion = seq * 2ull;
            const auto& slot = _audioRing[seq % kAudioRingCapacity];
            const auto v1 = slot.version.load(std::memory_order_acquire);
            if (v1 != expectedVersion) {
                continue;
            }

            const auto feature = slot.feature;
            const auto v2 = slot.version.load(std::memory_order_acquire);
            if (v2 != expectedVersion) {
                continue;
            }

            if (feature.qpcStart == 0 || feature.qpcEnd == 0 || feature.voiceId == 0) {
                continue;
            }
            if ((feature.qpcEnd + lookbackUs) < truthUs || feature.qpcStart > (truthUs + lookaheadUs)) {
                continue;
            }

            ++out.windowCandidates;

            std::optional<TraceMeta> trace{};
            float voiceScore = 1.0f;
            std::uint64_t bindingAgeUs = 0;
            if (feature.instanceId != 0) {
                trace = InstanceTraceCache::GetSingleton().TryGet(feature.instanceId);
            } else {
                const auto voicePtr = static_cast<std::uintptr_t>(feature.voiceId);
                const auto binding = VoiceBindingMap::GetSingleton().TryGet(voicePtr);
                if (!binding.has_value()) {
                    ++out.bindingMissCandidates;
                    continue;
                }

                if (feature.voiceGeneration != 0 && binding->generation != feature.voiceGeneration) {
                    ++out.bindingMissCandidates;
                    continue;
                }

                bindingAgeUs = (truthUs > binding->tsUs) ? (truthUs - binding->tsUs) : 0ull;
                if (bindingAgeUs > ttlUs) {
                    ++out.bindingMissCandidates;
                    continue;
                }

                voiceScore = Clamp01(1.0f - (static_cast<float>(bindingAgeUs) / static_cast<float>(ttlUs)));
                trace = InstanceTraceCache::GetSingleton().TryGet(binding->instanceId);
            }

            if (!trace.has_value()) {
                ++out.traceMissCandidates;
                continue;
            }

            float metaConfidence = 0.0f;
            if (!ResolveFootstepMeta(*trace, metaConfidence)) {
                continue;
            }

            ++out.semanticCandidates;

            const auto deltaUs = ComputeTimeDistanceUs(truthUs, feature);
            const auto durationUs = static_cast<std::uint32_t>(
                std::clamp<std::uint64_t>(
                    (feature.qpcEnd > feature.qpcStart) ? (feature.qpcEnd - feature.qpcStart) : 0ull,
                    0ull,
                    std::numeric_limits<std::uint32_t>::max()));
            const auto sum = std::max(1e-4f, feature.energyL + feature.energyR);
            const auto pan = std::clamp((feature.energyR - feature.energyL) / sum, -1.0f, 1.0f);
            const auto panAbs = std::abs(pan);
            const auto timeScore = Clamp01(std::exp(-static_cast<float>(deltaUs) / kTimeTauUs));
            const auto energyScore = Clamp01(0.65f * feature.peak + 0.35f * feature.rms);
            const auto score = Clamp01(
                0.35f * timeScore +
                0.35f * Clamp01(metaConfidence) +
                0.15f * voiceScore +
                0.15f * energyScore);

            const bool better =
                !haveBest ||
                score > out.bestScore ||
                (score == out.bestScore && deltaUs < out.bestDeltaUs);
            if (!better) {
                continue;
            }

            haveBest = true;
            out.bestScore = score;
            out.bestDeltaUs = deltaUs;
            if (score >= cfg.footstepAudioMatcherMinScore) {
                out.matched = true;
                out.matchDeltaUs = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(deltaUs, std::numeric_limits<std::uint32_t>::max()));
                out.matchDurationUs = durationUs;
                out.matchScorePermille = static_cast<std::uint16_t>(
                    std::clamp(score * 1000.0f, 0.0f, 1000.0f));
                out.matchPanAbsPermille = static_cast<std::uint16_t>(
                    std::clamp(panAbs * 1000.0f, 0.0f, 1000.0f));
                out.matchPanSigned = pan;
                out.matchEnergyScore = energyScore;
            }
        }

        if (!haveBest) {
            out.bestDeltaUs = std::numeric_limits<std::uint64_t>::max();
        }
        return out;
    }

    void FootstepAudioMatcher::CommitMatchSamples(const MatchResult& match)
    {
        std::scoped_lock lock(_sampleMutex);
        if (_matchDeltasUs.size() >= kSampleCap) {
            _matchDeltasUs.erase(_matchDeltasUs.begin());
        }
        if (_matchDurationsUs.size() >= kSampleCap) {
            _matchDurationsUs.erase(_matchDurationsUs.begin());
        }
        if (_matchScoresPermille.size() >= kSampleCap) {
            _matchScoresPermille.erase(_matchScoresPermille.begin());
        }
        if (_matchPanAbsPermille.size() >= kSampleCap) {
            _matchPanAbsPermille.erase(_matchPanAbsPermille.begin());
        }
        _matchDeltasUs.push_back(match.matchDeltaUs);
        _matchDurationsUs.push_back(match.matchDurationUs);
        _matchScoresPermille.push_back(match.matchScorePermille);
        _matchPanAbsPermille.push_back(match.matchPanAbsPermille);
    }

    void FootstepAudioMatcher::QueueLivePatch(
        const PendingPatch& patch,
        const MatchResult& match,
        std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioModifierLivePatch ||
            cfg.enableFootstepTruthSessionLivePatch ||
            !match.bridgeMatched ||
            !match.matched) {
            return;
        }

        LivePatch live{};
        live.truthUs = patch.truthUs;
        live.expireUs = nowUs + kLivePatchTtlUs;
        live.targetEndUs = patch.truthUs + std::clamp<std::uint64_t>(
            static_cast<std::uint64_t>(match.matchDeltaUs) +
                static_cast<std::uint64_t>(match.matchDurationUs),
            32000ull,
            static_cast<std::uint64_t>(cfg.stateTrackFootstepPatchLeaseUs));
        live.ampScale = std::clamp(0.90f + 0.28f * match.matchEnergyScore, 0.90f, 1.18f);
        live.panSigned = std::clamp(match.matchPanSigned, -0.55f, 0.55f);
        live.patchLeaseUs = cfg.stateTrackFootstepPatchLeaseUs;
        live.scorePermille = match.matchScorePermille;
        live.fromRecentMemory = false;
        live.bucket = ClassifyMemoryBucket(patch.tag);
        for (auto it = _truthHints.rbegin(); it != _truthHints.rend(); ++it) {
            if (it->truthUs == live.truthUs) {
                live.truthGapUs = it->truthGapUs;
                if (live.bucket == MemoryBucket::Unknown) {
                    live.bucket = it->bucket;
                }
                break;
            }
        }

        std::scoped_lock lock(_pendingMutex);
        while (!_readyLivePatches.empty() && _readyLivePatches.front().expireUs <= nowUs) {
            _readyLivePatches.pop_front();
            _livePatchesExpired.fetch_add(1, std::memory_order_relaxed);
        }

        for (auto it = _readyLivePatches.begin(); it != _readyLivePatches.end(); ++it) {
            if (it->truthUs != live.truthUs) {
                continue;
            }
            *it = live;
            return;
        }

        _readyLivePatches.push_back(live);
        _livePatchesQueued.fetch_add(1, std::memory_order_relaxed);
    }

    void FootstepAudioMatcher::RememberRecentModifierLocked(const LivePatch& patch, std::uint64_t nowUs)
    {
        if (patch.fromRecentMemory) {
            return;
        }

        RecentModifierSample sample{};
        sample.observedUs = nowUs;
        sample.truthUs = patch.truthUs;
        sample.ampScale = patch.ampScale;
        sample.panSigned = patch.panSigned;
        sample.targetEndDeltaUs = static_cast<std::uint32_t>(
            (patch.targetEndUs > patch.truthUs) ? (patch.targetEndUs - patch.truthUs) : 0ull);
        sample.scorePermille = patch.scorePermille;
        sample.bucket = patch.bucket;
        sample.truthGapUs = patch.truthGapUs;

        _recentModifierSamples.push_back(sample);
        while (_recentModifierSamples.size() > 16) {
            _recentModifierSamples.pop_front();
        }
        _recentMemorySamples.store(
            static_cast<std::uint64_t>(_recentModifierSamples.size()),
            std::memory_order_relaxed);
    }

    void FootstepAudioMatcher::ExpireRecentModifierLocked(std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        while (!_recentModifierSamples.empty()) {
            const auto ageUs = (nowUs > _recentModifierSamples.front().observedUs) ?
                (nowUs - _recentModifierSamples.front().observedUs) :
                0ull;
            if (ageUs <= static_cast<std::uint64_t>(cfg.footstepRecentModifierMemoryMaxAgeUs)) {
                break;
            }
            _recentModifierSamples.pop_front();
        }
        _recentMemorySamples.store(
            static_cast<std::uint64_t>(_recentModifierSamples.size()),
            std::memory_order_relaxed);

        while (!_truthHints.empty()) {
            const auto ageUs = (nowUs > _truthHints.front().truthUs) ?
                (nowUs - _truthHints.front().truthUs) :
                0ull;
            if (ageUs <= static_cast<std::uint64_t>(cfg.footstepRecentModifierMemoryMaxAgeUs)) {
                break;
            }
            _truthHints.pop_front();
        }
    }

    std::uint32_t FootstepAudioMatcher::PercentileOf(std::vector<std::uint32_t> values, float p)
    {
        if (values.empty()) {
            return 0;
        }

        const auto clamped = std::clamp(p, 0.0f, 1.0f);
        const auto idx = static_cast<std::size_t>(clamped * static_cast<float>(values.size() - 1));
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
        return values[idx];
    }
}

