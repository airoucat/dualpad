#include "pch.h"
#include "haptics/WeaponSwingTruthProbe.h"

#include "haptics/HapticsConfig.h"
#include "haptics/HidOutput.h"
#include "haptics/HapticsTypes.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <string>
#include <string_view>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kMaxProbeLinesPerSecond = 10;
        constexpr float kTruthSwingOppositeMotorScale = 0.82f;
        constexpr std::uint64_t kRegisterRetryIntervalUs = 1000000ull;
        constexpr std::uint64_t kSwingTriggerDedupeUs = 45000ull;
        constexpr std::uint64_t kSwingFollowupMergeUs = 700000ull;

        enum class SwingTruthKind
        {
            None = 0,
            Swing,
            PowerSwing,
            IgnoreAttackPhase
        };

        struct SwingTruthTagMeta
        {
            SwingTruthKind kind{ SwingTruthKind::None };
            bool attackLike{ false };
            bool left{ false };
            bool right{ false };
            bool followupStart{ false };
            float pulseScale{ 1.0f };
            float oppositeScale{ kTruthSwingOppositeMotorScale };
            const char* normalized{ "None" };
        };

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

        std::string ToLowerAscii(std::string_view view)
        {
            std::string out;
            out.reserve(view.size());
            for (char c : view) {
                if (c >= 'A' && c <= 'Z') {
                    out.push_back(static_cast<char>(c - 'A' + 'a'));
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }

        std::string_view TrimTag(const char* tag)
        {
            if (!tag || tag[0] == '\0') {
                return "<none>";
            }

            constexpr std::size_t kMaxTagLen = 64;
            auto view = std::string_view(tag);
            if (view.size() > kMaxTagLen) {
                view = view.substr(0, kMaxTagLen);
            }
            return view;
        }

        SwingTruthTagMeta ClassifySwingTruthTag(std::string_view tag)
        {
            const auto lower = ToLowerAscii(tag);

            SwingTruthTagMeta meta{};
            meta.attackLike =
                lower.find("swing") != std::string::npos ||
                lower.find("attack") != std::string::npos ||
                lower.find("weapon") != std::string::npos ||
                lower.find("bash") != std::string::npos ||
                lower.find("block") != std::string::npos;
            meta.left = lower.find("left") != std::string::npos;
            meta.right = lower.find("right") != std::string::npos;

            if (lower.find("powerattack_start") != std::string::npos ||
                lower.find("powerattack") != std::string::npos) {
                meta.kind = SwingTruthKind::PowerSwing;
                meta.followupStart = true;
                meta.pulseScale = 1.16f;
                meta.oppositeScale = 0.76f;
                meta.normalized = meta.left ? "PowerSwingLeft" : (meta.right ? "PowerSwingRight" : "PowerSwing");
                return meta;
            }

            if (lower.find("attackwinstart") != std::string::npos) {
                meta.kind = SwingTruthKind::Swing;
                meta.followupStart = true;
                meta.pulseScale = 1.06f;
                meta.oppositeScale = 0.78f;
                meta.normalized = meta.left ? "AttackStartLeft" : (meta.right ? "AttackStartRight" : "AttackStart");
                return meta;
            }

            if (lower.find("attackwinend") != std::string::npos ||
                lower.find("attackstop") != std::string::npos ||
                lower.find("_end") != std::string::npos) {
                meta.kind = SwingTruthKind::IgnoreAttackPhase;
                meta.normalized = "AttackPhaseEnd";
                return meta;
            }

            if (lower.find("weaponleftswing") != std::string::npos ||
                lower.find("leftswing") != std::string::npos) {
                meta.kind = SwingTruthKind::Swing;
                meta.left = true;
                meta.right = false;
                meta.pulseScale = 1.04f;
                meta.oppositeScale = 0.78f;
                meta.normalized = "WeaponSwingLeft";
                return meta;
            }

            if (lower.find("weaponrightswing") != std::string::npos ||
                lower.find("rightswing") != std::string::npos) {
                meta.kind = SwingTruthKind::Swing;
                meta.left = false;
                meta.right = true;
                meta.pulseScale = 1.04f;
                meta.oppositeScale = 0.78f;
                meta.normalized = "WeaponSwingRight";
                return meta;
            }

            if (lower.find("weaponswing") != std::string::npos) {
                meta.kind = SwingTruthKind::Swing;
                meta.normalized = "WeaponSwing";
                return meta;
            }

            return meta;
        }

        bool LooksAttackLikeTag(std::string_view tag)
        {
            return ClassifySwingTruthTag(tag).attackLike;
        }
    }

    WeaponSwingTruthProbe& WeaponSwingTruthProbe::GetSingleton()
    {
        static WeaponSwingTruthProbe instance;
        return instance;
    }

    void WeaponSwingTruthProbe::ResetStats()
    {
        _totalEvents.store(0, std::memory_order_relaxed);
        _playerEvents.store(0, std::memory_order_relaxed);
        _swingMatched.store(0, std::memory_order_relaxed);
        _swingSubmitted.store(0, std::memory_order_relaxed);
        _attackLikeRejected.store(0, std::memory_order_relaxed);
    }

    bool WeaponSwingTruthProbe::Register()
    {
        _wantRegistered.store(true, std::memory_order_release);
        if (_registered.load(std::memory_order_acquire)) {
            return true;
        }
        return TryRegisterSink(true);
    }

    bool WeaponSwingTruthProbe::TryRegisterSink(bool logFailure)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            if (logFailure) {
                logger::warn("[Haptics][SwingTruth] register failed: player unavailable");
            }
            return false;
        }

        if (!_statsResetForSession.exchange(true, std::memory_order_acq_rel)) {
            ResetStats();
        }

        const bool registered = player->AddAnimationGraphEventSink(this);
        if (!registered) {
            if (logFailure) {
                logger::warn("[Haptics][SwingTruth] register failed: animation graph unavailable");
            }
            _registered.store(false, std::memory_order_release);
            return false;
        }

        _registered.store(true, std::memory_order_release);
        logger::info("[Haptics][SwingTruth] registered BSAnimationGraphEvent sink");
        return true;
    }

    void WeaponSwingTruthProbe::Unregister()
    {
        _wantRegistered.store(false, std::memory_order_release);
        _lastRegisterAttemptUs.store(0, std::memory_order_relaxed);
        _statsResetForSession.store(false, std::memory_order_release);
        if (!_registered.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (auto* player = RE::PlayerCharacter::GetSingleton(); player) {
            player->RemoveAnimationGraphEventSink(this);
        }
        logger::info("[Haptics][SwingTruth] unregistered BSAnimationGraphEvent sink");
    }

    void WeaponSwingTruthProbe::Tick(std::uint64_t nowUs)
    {
        if (!_wantRegistered.load(std::memory_order_acquire) ||
            _registered.load(std::memory_order_acquire)) {
            return;
        }

        const auto lastAttemptUs = _lastRegisterAttemptUs.load(std::memory_order_relaxed);
        if (lastAttemptUs != 0 && nowUs > lastAttemptUs &&
            (nowUs - lastAttemptUs) < kRegisterRetryIntervalUs) {
            return;
        }

        _lastRegisterAttemptUs.store(nowUs, std::memory_order_relaxed);
        TryRegisterSink(true);
    }

    WeaponSwingTruthProbe::Stats WeaponSwingTruthProbe::GetStats() const
    {
        Stats s;
        s.totalEvents = _totalEvents.load(std::memory_order_relaxed);
        s.playerEvents = _playerEvents.load(std::memory_order_relaxed);
        s.swingMatched = _swingMatched.load(std::memory_order_relaxed);
        s.swingSubmitted = _swingSubmitted.load(std::memory_order_relaxed);
        s.attackLikeRejected = _attackLikeRejected.load(std::memory_order_relaxed);
        return s;
    }

    RE::BSEventNotifyControl WeaponSwingTruthProbe::ProcessEvent(
        const RE::BSAnimationGraphEvent* event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        if (!event || !event->holder) {
            return RE::BSEventNotifyControl::kContinue;
        }

        _totalEvents.fetch_add(1, std::memory_order_relaxed);

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || event->holder != player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        _playerEvents.fetch_add(1, std::memory_order_relaxed);

        const auto nowUs = ToQPC(Now());
        const auto tagView = TrimTag(event->tag.c_str());
        const auto meta = ClassifySwingTruthTag(tagView);
        const bool isSwing =
            meta.kind == SwingTruthKind::Swing ||
            meta.kind == SwingTruthKind::PowerSwing;

        if (!isSwing) {
            if (LooksAttackLikeTag(tagView)) {
                _attackLikeRejected.fetch_add(1, std::memory_order_relaxed);
                if (ShouldEmitWindowedProbe(
                        s_probeWindowUs,
                        s_probeLines,
                        nowUs,
                        kMaxProbeLinesPerSecond)) {
                    logger::info(
                        "[Haptics][SwingTruth] site=truth/reject tag={} reason={}",
                        tagView,
                        meta.kind == SwingTruthKind::IgnoreAttackPhase ?
                            "ignored_attack_phase" :
                            "unmapped_attack_like");
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        _swingMatched.fetch_add(1, std::memory_order_relaxed);

        const auto lastSubmitUs = _lastSwingSubmitUs.load(std::memory_order_relaxed);
        if (meta.followupStart &&
            lastSubmitUs != 0 &&
            nowUs > lastSubmitUs &&
            (nowUs - lastSubmitUs) < kSwingFollowupMergeUs) {
            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    nowUs,
                    kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][SwingTruth] site=truth/merge tag={} norm={} deltaUs={} reason=followup_recent_swing",
                    tagView,
                    meta.normalized,
                    static_cast<unsigned long long>(nowUs - lastSubmitUs));
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (lastSubmitUs != 0 && nowUs > lastSubmitUs &&
            (nowUs - lastSubmitUs) < kSwingTriggerDedupeUs) {
            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    nowUs,
                    kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][SwingTruth] site=truth/merge tag={} norm={} deltaUs={}",
                    tagView,
                    meta.normalized,
                    static_cast<unsigned long long>(nowUs - lastSubmitUs));
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableStateTrackWeaponSwingTruthTrigger) {
            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    nowUs,
                    kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][SwingTruth] site=truth/match tag={} action=disabled",
                    tagView);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto basePulse = std::clamp(cfg.basePulseSwing * meta.pulseScale, 0.12f, 1.0f);
        const auto baseMotor = static_cast<std::uint8_t>(std::clamp(
            basePulse * 255.0f,
            0.0f,
            255.0f));

        std::uint8_t leftMotor = baseMotor;
        std::uint8_t rightMotor = baseMotor;
        const bool isLeft = meta.left;
        const bool isRight = meta.right;
        if (isLeft && !isRight) {
            rightMotor = static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(baseMotor) * meta.oppositeScale,
                0.0f,
                255.0f));
        } else if (isRight && !isLeft) {
            leftMotor = static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(baseMotor) * meta.oppositeScale,
                0.0f,
                255.0f));
        }

        HidFrame frame{};
        frame.qpc = nowUs;
        frame.qpcTarget = nowUs;
        frame.eventType = EventType::WeaponSwing;
        frame.sourceType = SourceType::BaseEvent;
        frame.priority = static_cast<std::uint8_t>(std::clamp(cfg.prioritySwing, 1, 255));
        frame.confidence = 1.0f;
        frame.foregroundHint = true;
        frame.leftMotor = leftMotor;
        frame.rightMotor = rightMotor;
        const bool submitted = HidOutput::GetSingleton().SubmitFrameNonBlocking(frame);
        if (submitted) {
            _swingSubmitted.fetch_add(1, std::memory_order_relaxed);
            _lastSwingSubmitUs.store(nowUs, std::memory_order_relaxed);
        }

        if (ShouldEmitWindowedProbe(
                s_probeWindowUs,
                s_probeLines,
                nowUs,
                kMaxProbeLinesPerSecond)) {
            logger::info(
                "[Haptics][SwingTruth] site=truth/submit tag={} norm={} submitted={} motor={}/{}",
                tagView,
                meta.normalized,
                submitted ? 1 : 0,
                static_cast<int>(leftMotor),
                static_cast<int>(rightMotor));
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
