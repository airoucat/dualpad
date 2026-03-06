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

        bool IsWeaponSwingTruthTag(std::string_view tag)
        {
            const auto lower = ToLowerAscii(tag);
            return
                lower.find("weaponswing") != std::string::npos ||
                lower.find("weaponleftswing") != std::string::npos ||
                lower.find("weaponrightswing") != std::string::npos ||
                lower.find("leftswing") != std::string::npos ||
                lower.find("rightswing") != std::string::npos;
        }

        bool LooksAttackLikeTag(std::string_view tag)
        {
            const auto lower = ToLowerAscii(tag);
            return
                lower.find("swing") != std::string::npos ||
                lower.find("attack") != std::string::npos ||
                lower.find("weapon") != std::string::npos ||
                lower.find("bash") != std::string::npos ||
                lower.find("block") != std::string::npos;
        }

        bool IsLeftSwingTag(std::string_view tag)
        {
            return ToLowerAscii(tag).find("left") != std::string::npos;
        }

        bool IsRightSwingTag(std::string_view tag)
        {
            return ToLowerAscii(tag).find("right") != std::string::npos;
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
        if (_registered.exchange(true, std::memory_order_acq_rel)) {
            return true;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            _registered.store(false, std::memory_order_release);
            logger::warn("[Haptics][SwingTruth] register failed: player unavailable");
            return false;
        }

        ResetStats();
        player->AddAnimationGraphEventSink(this);
        logger::info("[Haptics][SwingTruth] registered BSAnimationGraphEvent sink");
        return true;
    }

    void WeaponSwingTruthProbe::Unregister()
    {
        if (!_registered.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (auto* player = RE::PlayerCharacter::GetSingleton(); player) {
            player->RemoveAnimationGraphEventSink(this);
        }
        logger::info("[Haptics][SwingTruth] unregistered BSAnimationGraphEvent sink");
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
        const bool isSwing = IsWeaponSwingTruthTag(tagView);

        if (!isSwing) {
            if (LooksAttackLikeTag(tagView)) {
                _attackLikeRejected.fetch_add(1, std::memory_order_relaxed);
                if (ShouldEmitWindowedProbe(
                        s_probeWindowUs,
                        s_probeLines,
                        nowUs,
                        kMaxProbeLinesPerSecond)) {
                    logger::info(
                        "[Haptics][SwingTruth] site=truth/reject tag={} reason=unmapped_attack_like",
                        tagView);
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        _swingMatched.fetch_add(1, std::memory_order_relaxed);

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

        const auto basePulse = std::clamp(cfg.basePulseSwing, 0.10f, 1.0f);
        const auto baseMotor = static_cast<std::uint8_t>(std::clamp(
            basePulse * 255.0f,
            0.0f,
            255.0f));

        std::uint8_t leftMotor = baseMotor;
        std::uint8_t rightMotor = baseMotor;
        const bool isLeft = IsLeftSwingTag(tagView);
        const bool isRight = IsRightSwingTag(tagView);
        if (isLeft && !isRight) {
            rightMotor = static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(baseMotor) * kTruthSwingOppositeMotorScale,
                0.0f,
                255.0f));
        } else if (isRight && !isLeft) {
            leftMotor = static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(baseMotor) * kTruthSwingOppositeMotorScale,
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
        }

        if (ShouldEmitWindowedProbe(
                s_probeWindowUs,
                s_probeLines,
                nowUs,
                kMaxProbeLinesPerSecond)) {
            logger::info(
                "[Haptics][SwingTruth] site=truth/submit tag={} submitted={} motor={}/{}",
                tagView,
                submitted ? 1 : 0,
                static_cast<int>(leftMotor),
                static_cast<int>(rightMotor));
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
