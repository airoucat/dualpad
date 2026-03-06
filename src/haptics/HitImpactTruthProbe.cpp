#include "pch.h"
#include "haptics/HitImpactTruthProbe.h"

#include "haptics/HapticsConfig.h"
#include "haptics/HidOutput.h"
#include "haptics/HapticsTypes.h"

#include <SKSE/SKSE.h>
#include <algorithm>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kMaxProbeLinesPerSecond = 12;

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

        const char* RoleLabel(bool playerIsCause, bool playerIsTarget)
        {
            if (playerIsCause && playerIsTarget) {
                return "both";
            }
            if (playerIsCause) {
                return "cause";
            }
            if (playerIsTarget) {
                return "target";
            }
            return "other";
        }

        float BuildPulseScale(
            EventType eventType,
            bool playerIsCause,
            bool playerIsTarget,
            bool powerAttack,
            bool bashAttack,
            bool sneakAttack)
        {
            float scale = (eventType == EventType::Block) ? 0.80f : 0.92f;
            if (playerIsTarget) {
                scale += 0.10f;
            }
            if (playerIsCause && !playerIsTarget) {
                scale -= 0.06f;
            }
            if (powerAttack) {
                scale += 0.08f;
            }
            if (bashAttack) {
                scale -= 0.04f;
            }
            if (sneakAttack) {
                scale += 0.05f;
            }
            return std::clamp(scale, 0.35f, 1.20f);
        }
    }

    HitImpactTruthProbe& HitImpactTruthProbe::GetSingleton()
    {
        static HitImpactTruthProbe instance;
        return instance;
    }

    void HitImpactTruthProbe::ResetStats()
    {
        _totalEvents.store(0, std::memory_order_relaxed);
        _playerEvents.store(0, std::memory_order_relaxed);
        _playerCauseEvents.store(0, std::memory_order_relaxed);
        _playerTargetEvents.store(0, std::memory_order_relaxed);
        _hitMatched.store(0, std::memory_order_relaxed);
        _blockMatched.store(0, std::memory_order_relaxed);
        _hitSubmitted.store(0, std::memory_order_relaxed);
        _blockSubmitted.store(0, std::memory_order_relaxed);
    }

    bool HitImpactTruthProbe::Register()
    {
        if (_registered.exchange(true, std::memory_order_acq_rel)) {
            return true;
        }

        auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
        if (!eventSource) {
            _registered.store(false, std::memory_order_release);
            logger::warn("[Haptics][HitTruth] register failed: ScriptEventSourceHolder unavailable");
            return false;
        }

        ResetStats();
        eventSource->AddEventSink<RE::TESHitEvent>(this);
        logger::info("[Haptics][HitTruth] registered TESHitEvent sink");
        return true;
    }

    void HitImpactTruthProbe::Unregister()
    {
        if (!_registered.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton(); eventSource) {
            eventSource->RemoveEventSink<RE::TESHitEvent>(this);
        }
        logger::info("[Haptics][HitTruth] unregistered TESHitEvent sink");
    }

    HitImpactTruthProbe::Stats HitImpactTruthProbe::GetStats() const
    {
        Stats s;
        s.totalEvents = _totalEvents.load(std::memory_order_relaxed);
        s.playerEvents = _playerEvents.load(std::memory_order_relaxed);
        s.playerCauseEvents = _playerCauseEvents.load(std::memory_order_relaxed);
        s.playerTargetEvents = _playerTargetEvents.load(std::memory_order_relaxed);
        s.hitMatched = _hitMatched.load(std::memory_order_relaxed);
        s.blockMatched = _blockMatched.load(std::memory_order_relaxed);
        s.hitSubmitted = _hitSubmitted.load(std::memory_order_relaxed);
        s.blockSubmitted = _blockSubmitted.load(std::memory_order_relaxed);
        return s;
    }

    RE::BSEventNotifyControl HitImpactTruthProbe::ProcessEvent(
        const RE::TESHitEvent* event,
        RE::BSTEventSource<RE::TESHitEvent>*)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        _totalEvents.fetch_add(1, std::memory_order_relaxed);

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* target = event->target.get();
        auto* cause = event->cause.get();
        const bool playerIsTarget = (target == player);
        const bool playerIsCause = (cause == player);
        if (!playerIsCause && !playerIsTarget) {
            return RE::BSEventNotifyControl::kContinue;
        }

        _playerEvents.fetch_add(1, std::memory_order_relaxed);
        if (playerIsCause) {
            _playerCauseEvents.fetch_add(1, std::memory_order_relaxed);
        }
        if (playerIsTarget) {
            _playerTargetEvents.fetch_add(1, std::memory_order_relaxed);
        }

        const bool blocked = event->flags.all(RE::TESHitEvent::Flag::kHitBlocked);
        const bool powerAttack = event->flags.all(RE::TESHitEvent::Flag::kPowerAttack);
        const bool bashAttack = event->flags.all(RE::TESHitEvent::Flag::kBashAttack);
        const bool sneakAttack = event->flags.all(RE::TESHitEvent::Flag::kSneakAttack);
        const auto eventType = blocked ? EventType::Block : EventType::HitImpact;
        if (blocked) {
            _blockMatched.fetch_add(1, std::memory_order_relaxed);
        } else {
            _hitMatched.fetch_add(1, std::memory_order_relaxed);
        }

        const auto nowUs = ToQPC(Now());
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableStateTrackHitTruthTrigger) {
            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    nowUs,
                    kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][HitTruth] site=truth/match evt={} role={} blocked={} action=disabled",
                    ToString(eventType),
                    RoleLabel(playerIsCause, playerIsTarget),
                    blocked ? 1 : 0);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        const float pulseScale = BuildPulseScale(
            eventType,
            playerIsCause,
            playerIsTarget,
            powerAttack,
            bashAttack,
            sneakAttack);
        const auto basePulse = std::clamp(cfg.basePulseHit * pulseScale, 0.12f, 1.0f);
        const auto motor = static_cast<std::uint8_t>(std::clamp(
            basePulse * 255.0f,
            0.0f,
            255.0f));

        HidFrame frame{};
        frame.qpc = nowUs;
        frame.qpcTarget = nowUs;
        frame.eventType = eventType;
        frame.sourceType = SourceType::BaseEvent;
        frame.priority = static_cast<std::uint8_t>(std::clamp(cfg.priorityHit, 1, 255));
        frame.confidence = 1.0f;
        frame.foregroundHint = true;
        frame.leftMotor = motor;
        frame.rightMotor = motor;
        const bool submitted = HidOutput::GetSingleton().SubmitFrameNonBlocking(frame);
        if (submitted) {
            if (blocked) {
                _blockSubmitted.fetch_add(1, std::memory_order_relaxed);
            } else {
                _hitSubmitted.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (ShouldEmitWindowedProbe(
                s_probeWindowUs,
                s_probeLines,
                nowUs,
                kMaxProbeLinesPerSecond)) {
            logger::info(
                "[Haptics][HitTruth] site=truth/submit evt={} role={} blocked={} submitted={} motor={}/{} src=0x{:08X} proj=0x{:08X} flags(p/s/b/bl)={}/{}/{}/{}",
                ToString(eventType),
                RoleLabel(playerIsCause, playerIsTarget),
                blocked ? 1 : 0,
                submitted ? 1 : 0,
                static_cast<int>(motor),
                static_cast<int>(motor),
                event->source,
                event->projectile,
                powerAttack ? 1 : 0,
                sneakAttack ? 1 : 0,
                bashAttack ? 1 : 0,
                blocked ? 1 : 0);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
