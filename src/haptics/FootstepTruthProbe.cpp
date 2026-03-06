#include "pch.h"
#include "haptics/FootstepTruthProbe.h"

#include "haptics/FootstepAudioMatcher.h"
#include "haptics/FootstepTruthBridge.h"
#include "haptics/HapticsConfig.h"
#include "haptics/HidOutput.h"
#include "input/InputContext.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <limits>
#include <string_view>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kMaxProbeLinesPerSecond = 10;
        constexpr std::uint32_t kMaxShadowProbeLinesPerSecond = 6;
        constexpr std::uint64_t kShadowTruthExpireUs = 80000ull;
        constexpr std::size_t kShadowDeltaSampleCap = 1024;
        constexpr float kTruthFootOppositeMotorScale = 0.88f;

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

        std::string_view TrimTag(const char* tag)
        {
            if (!tag || tag[0] == '\0') {
                return "<none>";
            }

            constexpr std::size_t kMaxTagLen = 48;
            auto view = std::string_view(tag);
            if (view.size() > kMaxTagLen) {
                view = view.substr(0, kMaxTagLen);
            }
            return view;
        }
    }

    FootstepTruthProbe& FootstepTruthProbe::GetSingleton()
    {
        static FootstepTruthProbe instance;
        return instance;
    }

    void FootstepTruthProbe::ResetStats()
    {
        _totalEvents.store(0, std::memory_order_relaxed);
        _actorResolvedEvents.store(0, std::memory_order_relaxed);
        _playerEvents.store(0, std::memory_order_relaxed);
        _nonPlayerEvents.store(0, std::memory_order_relaxed);
        _contextAllowedEvents.store(0, std::memory_order_relaxed);
        _contextBlockedEvents.store(0, std::memory_order_relaxed);
        _movingEvents.store(0, std::memory_order_relaxed);
        _recentMoveEvents.store(0, std::memory_order_relaxed);
        _admissibleEvents.store(0, std::memory_order_relaxed);
        _shadowMatchedRenders.store(0, std::memory_order_relaxed);
        _shadowExpiredTruthMisses.store(0, std::memory_order_relaxed);
        _shadowRenderWithoutTruth.store(0, std::memory_order_relaxed);
        std::scoped_lock lock(_shadowMutex);
        _pendingTruth.clear();
        _shadowRenderDeltasUs.clear();
    }

    bool FootstepTruthProbe::Register()
    {
        if (_registered.exchange(true, std::memory_order_acq_rel)) {
            return true;
        }

        auto* manager = RE::BGSFootstepManager::GetSingleton();
        if (!manager) {
            _registered.store(false, std::memory_order_release);
            logger::warn("[Haptics][FootTruth] register failed: BGSFootstepManager unavailable");
            return false;
        }

        ResetStats();
        manager->AddEventSink(this);
        logger::info("[Haptics][FootTruth] registered BGSFootstepEvent sink");
        return true;
    }

    void FootstepTruthProbe::Unregister()
    {
        if (!_registered.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (auto* manager = RE::BGSFootstepManager::GetSingleton(); manager) {
            manager->RemoveEventSink(this);
        }
        logger::info("[Haptics][FootTruth] unregistered BGSFootstepEvent sink");
    }

    FootstepTruthProbe::Stats FootstepTruthProbe::GetStats() const
    {
        const auto nowUs = ToQPC(Now());
        {
            std::scoped_lock lock(_shadowMutex);
            const_cast<FootstepTruthProbe*>(this)->ExpireShadowTruthLocked(nowUs);
        }

        Stats s;
        s.totalEvents = _totalEvents.load(std::memory_order_relaxed);
        s.actorResolvedEvents = _actorResolvedEvents.load(std::memory_order_relaxed);
        s.playerEvents = _playerEvents.load(std::memory_order_relaxed);
        s.nonPlayerEvents = _nonPlayerEvents.load(std::memory_order_relaxed);
        s.contextAllowedEvents = _contextAllowedEvents.load(std::memory_order_relaxed);
        s.contextBlockedEvents = _contextBlockedEvents.load(std::memory_order_relaxed);
        s.movingEvents = _movingEvents.load(std::memory_order_relaxed);
        s.recentMoveEvents = _recentMoveEvents.load(std::memory_order_relaxed);
        s.admissibleEvents = _admissibleEvents.load(std::memory_order_relaxed);
        s.shadowMatchedRenders = _shadowMatchedRenders.load(std::memory_order_relaxed);
        s.shadowExpiredTruthMisses = _shadowExpiredTruthMisses.load(std::memory_order_relaxed);
        s.shadowRenderWithoutTruth = _shadowRenderWithoutTruth.load(std::memory_order_relaxed);
        {
            std::scoped_lock lock(_shadowMutex);
            s.shadowRenderDeltaP50Us = PercentileOf(_shadowRenderDeltasUs, 0.50f);
            s.shadowRenderDeltaP95Us = PercentileOf(_shadowRenderDeltasUs, 0.95f);
            s.shadowRenderDeltaSamples = static_cast<std::uint32_t>(_shadowRenderDeltasUs.size());
            s.shadowPendingTruth = static_cast<std::uint32_t>(_pendingTruth.size());
        }
        return s;
    }

    std::uint32_t FootstepTruthProbe::PercentileOf(std::vector<std::uint32_t> values, float p)
    {
        if (values.empty()) {
            return 0;
        }

        p = std::clamp(p, 0.0f, 1.0f);
        const auto idx = static_cast<std::size_t>(
            std::clamp<std::uint64_t>(
                static_cast<std::uint64_t>(p * static_cast<float>(values.size() - 1)),
                0,
                static_cast<std::uint64_t>(values.size() - 1)));
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
        return values[idx];
    }

    void FootstepTruthProbe::ExpireShadowTruthLocked(std::uint64_t nowUs)
    {
        static std::atomic<std::uint64_t> s_shadowProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_shadowProbeLines{ 0 };

        while (!_pendingTruth.empty()) {
            const auto& pending = _pendingTruth.front();
            if (pending.tsUs == 0 || nowUs <= pending.tsUs || (nowUs - pending.tsUs) <= kShadowTruthExpireUs) {
                break;
            }

            const auto ageUs = nowUs - pending.tsUs;
            const auto tag = pending.tag;
            _pendingTruth.pop_front();
            _shadowExpiredTruthMisses.fetch_add(1, std::memory_order_relaxed);

            if (ShouldEmitWindowedProbe(
                    s_shadowProbeWindowUs,
                    s_shadowProbeLines,
                    nowUs,
                    kMaxShadowProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][FootTruth] shadow_truth_expired tag={} age={}us pendingAfter={}",
                    tag,
                    ageUs,
                    _pendingTruth.size());
            }
        }
    }

    void FootstepTruthProbe::ObserveShadowRender(
        std::uint64_t renderUs,
        std::uint64_t seq,
        EventType eventType,
        std::uint8_t leftMotor,
        std::uint8_t rightMotor)
    {
        static std::atomic<std::uint64_t> s_shadowProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_shadowProbeLines{ 0 };

        if (eventType != EventType::Footstep || renderUs == 0) {
            return;
        }

        std::uint32_t matchedDeltaUs = 0;
        std::string matchedTag;
        std::uint32_t pendingAfter = 0;
        bool matched = false;
        {
            std::scoped_lock lock(_shadowMutex);
            ExpireShadowTruthLocked(renderUs);

            auto matchIt = _pendingTruth.end();
            std::uint64_t bestDeltaUs = std::numeric_limits<std::uint64_t>::max();
            for (auto it = _pendingTruth.begin(); it != _pendingTruth.end(); ++it) {
                if (it->tsUs == 0 || renderUs < it->tsUs) {
                    continue;
                }
                const auto deltaUs = renderUs - it->tsUs;
                if (deltaUs > kShadowTruthExpireUs) {
                    continue;
                }
                if (deltaUs < bestDeltaUs) {
                    bestDeltaUs = deltaUs;
                    matchIt = it;
                }
            }

            if (matchIt != _pendingTruth.end()) {
                matched = true;
                matchedDeltaUs = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(bestDeltaUs, std::numeric_limits<std::uint32_t>::max()));
                matchedTag = matchIt->tag;
                _pendingTruth.erase(matchIt);
                _shadowMatchedRenders.fetch_add(1, std::memory_order_relaxed);
                _shadowRenderDeltasUs.push_back(matchedDeltaUs);
                if (_shadowRenderDeltasUs.size() > kShadowDeltaSampleCap) {
                    _shadowRenderDeltasUs.erase(_shadowRenderDeltasUs.begin());
                }
            } else {
                _shadowRenderWithoutTruth.fetch_add(1, std::memory_order_relaxed);
            }
            pendingAfter = static_cast<std::uint32_t>(_pendingTruth.size());
        }

        if (!matched &&
            ShouldEmitWindowedProbe(
                s_shadowProbeWindowUs,
                s_shadowProbeLines,
                renderUs,
                kMaxShadowProbeLinesPerSecond)) {
            logger::info(
                "[Haptics][FootTruth] shadow_render_miss seq={} motor={}/{} pendingTruth={}",
                seq,
                static_cast<int>(leftMotor),
                static_cast<int>(rightMotor),
                pendingAfter);
        } else if (matched && matchedDeltaUs >= 20000u &&
            ShouldEmitWindowedProbe(
                s_shadowProbeWindowUs,
                s_shadowProbeLines,
                renderUs,
                kMaxShadowProbeLinesPerSecond)) {
            logger::info(
                "[Haptics][FootTruth] shadow_render_slow seq={} tag={} delta={}us pendingTruth={}",
                seq,
                matchedTag,
                matchedDeltaUs,
                pendingAfter);
        }
    }

    RE::BSEventNotifyControl FootstepTruthProbe::ProcessEvent(
        const RE::BGSFootstepEvent* event,
        RE::BSTEventSource<RE::BGSFootstepEvent>*)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        _totalEvents.fetch_add(1, std::memory_order_relaxed);

        const auto actorPtr = event->actor.get();
        if (!actorPtr) {
            return RE::BSEventNotifyControl::kContinue;
        }

        _actorResolvedEvents.fetch_add(1, std::memory_order_relaxed);

        auto* player = RE::PlayerCharacter::GetSingleton();
        const bool isPlayer = player && actorPtr.get() == player;
        if (!isPlayer) {
            _nonPlayerEvents.fetch_add(1, std::memory_order_relaxed);
            return RE::BSEventNotifyControl::kContinue;
        }

        _playerEvents.fetch_add(1, std::memory_order_relaxed);

        const auto nowUs = ToQPC(Now());
        const auto tagView = TrimTag(event->tag.c_str());
        const auto& cfg = HapticsConfig::GetSingleton();
        auto& ctxMgr = dualpad::input::ContextManager::GetSingleton();
        const auto currentContext = ctxMgr.GetCurrentContext();
        const bool contextAllowed = ctxMgr.IsFootstepContextAllowed();
        const bool moving = ctxMgr.IsPlayerMoving();
        const auto recentMoveWindowUs =
            static_cast<std::uint64_t>(cfg.stateTrackFootstepRecentMoveMs) * 1000ull;
        const bool recentMove = ctxMgr.WasPlayerMovingRecently(recentMoveWindowUs);
        const bool admissible = cfg.enableStateTrackFootstepTruthTrigger ?
            contextAllowed :
            (contextAllowed && (moving || recentMove));

        if (contextAllowed) {
            _contextAllowedEvents.fetch_add(1, std::memory_order_relaxed);
        } else {
            _contextBlockedEvents.fetch_add(1, std::memory_order_relaxed);
        }
        if (moving) {
            _movingEvents.fetch_add(1, std::memory_order_relaxed);
        }
        if (recentMove) {
            _recentMoveEvents.fetch_add(1, std::memory_order_relaxed);
        }
        if (admissible) {
            _admissibleEvents.fetch_add(1, std::memory_order_relaxed);
            std::scoped_lock lock(_shadowMutex);
            ExpireShadowTruthLocked(nowUs);
            _pendingTruth.push_back(ShadowTruthEvent{
                .tsUs = nowUs,
                .tag = std::string(tagView)
            });
            FootstepAudioMatcher::GetSingleton().ObserveTruthEvent(nowUs, tagView);
            FootstepTruthBridge::GetSingleton().ObserveTruthToken(nowUs, tagView);
        }

        if (admissible && cfg.enableStateTrackFootstepTruthTrigger) {
            const bool isLeft = tagView.find("Left") != std::string_view::npos;
            const bool isRight = tagView.find("Right") != std::string_view::npos;
            const auto basePulse = std::clamp(
                cfg.basePulseFootstep,
                0.08f,
                1.0f);
            const auto baseMotor = static_cast<std::uint8_t>(std::clamp(
                basePulse * 255.0f,
                0.0f,
                255.0f));

            std::uint8_t leftMotor = baseMotor;
            std::uint8_t rightMotor = baseMotor;
            if (isLeft && !isRight) {
                rightMotor = static_cast<std::uint8_t>(std::clamp(
                    static_cast<float>(baseMotor) * kTruthFootOppositeMotorScale,
                    0.0f,
                    255.0f));
            } else if (isRight && !isLeft) {
                leftMotor = static_cast<std::uint8_t>(std::clamp(
                    static_cast<float>(baseMotor) * kTruthFootOppositeMotorScale,
                    0.0f,
                    255.0f));
            }

            HidFrame frame{};
            frame.qpc = nowUs;
            frame.qpcTarget = nowUs;
            frame.eventType = EventType::Footstep;
            frame.sourceType = SourceType::BaseEvent;
            frame.priority = static_cast<std::uint8_t>(std::clamp(
                cfg.priorityFootstep,
                1,
                255));
            frame.confidence = 1.0f;
            frame.foregroundHint = true;
            frame.leftMotor = leftMotor;
            frame.rightMotor = rightMotor;
            (void)HidOutput::GetSingleton().SubmitFrameNonBlocking(frame);
        }

        if (ShouldEmitWindowedProbe(
                s_probeWindowUs,
                s_probeLines,
                nowUs,
                kMaxProbeLinesPerSecond)) {
            logger::info(
                "[Haptics][FootTruth] actor=player tag={} ctx={} moving={} recent={} admissible={} handle=0x{:X}",
                tagView,
                dualpad::input::ToString(currentContext),
                moving ? 1 : 0,
                recentMove ? 1 : 0,
                admissible ? 1 : 0,
                event->actor.native_handle());
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
