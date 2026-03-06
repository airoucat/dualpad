#include "pch.h"
#include "haptics/HidOutput.h"

#include "haptics/HapticsConfig.h"
#include "haptics/FootstepAudioMatcher.h"
#include "haptics/FootstepTruthSessionShadow.h"
#include "haptics/FootstepTruthProbe.h"
#include "input/InputContext.h"

#include <SKSE/SKSE.h>
#include <Windows.h>
#include <algorithm>
#include <hidapi/hidapi.h>
#include <limits>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::size_t kLatencySampleCap = 2048;
        constexpr std::uint64_t kFootstepStrideUpgradeCoalesceUs = 6000ull;

        enum class RouteReason : std::uint8_t
        {
            ZeroFrame = 0,
            ForegroundHint,
            PriorityForeground,
            EventForeground,
            UnknownBackground,
            BackgroundEvent
        };

        const char* RouteReasonLabel(RouteReason reason)
        {
            switch (reason) {
            case RouteReason::ZeroFrame:
                return "zero";
            case RouteReason::ForegroundHint:
                return "hint";
            case RouteReason::PriorityForeground:
                return "priority";
            case RouteReason::EventForeground:
                return "event";
            case RouteReason::UnknownBackground:
                return "unknown";
            case RouteReason::BackgroundEvent:
                return "background";
            default:
                return "other";
            }
        }

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

        bool IsRenderedTrackLane(const HapticsConfig& cfg, std::uint8_t index)
        {
            if (cfg.enableStateTrackImpactRenderer && index == 0) {
                return true;
            }
            if (cfg.enableStateTrackSwingRenderer && index == 1) {
                return true;
            }
            if (cfg.enableStateTrackFootstepRenderer && index == 2) {
                return true;
            }
            return false;
        }

        bool IsStructuredCombatEvent(EventType type)
        {
            switch (type) {
            case EventType::WeaponSwing:
            case EventType::HitImpact:
            case EventType::Block:
                return true;
            default:
                return false;
            }
        }

        bool IsTokenTrackLane(const HapticsConfig& cfg, std::uint8_t index)
        {
            return index == 2 &&
                cfg.enableStateTrackFootstepRenderer &&
                cfg.enableStateTrackFootstepTokenRenderer;
        }

        bool UsesCadenceLiveness(const HapticsConfig& cfg, std::uint8_t index)
        {
            return IsTokenTrackLane(cfg, index);
        }

        std::uint32_t FootstepPatchLeaseUs(const HapticsConfig& cfg, FootstepTruthGait gait)
        {
            const auto sprintLeaseBoostUs =
                (gait == FootstepTruthGait::Sprint) ? 40000ull : 0ull;
            return static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(cfg.stateTrackFootstepPatchLeaseUs) + sprintLeaseBoostUs,
                220000ull));
        }

        std::uint64_t FootstepTargetEndDeltaUs(const HapticsConfig& cfg, FootstepTruthGait gait)
        {
            const auto patchLeaseUs = static_cast<std::uint64_t>(FootstepPatchLeaseUs(cfg, gait));
            switch (gait) {
            case FootstepTruthGait::Sprint:
                return std::min<std::uint64_t>(160000ull, patchLeaseUs);
            case FootstepTruthGait::JumpUp:
            case FootstepTruthGait::JumpDown:
                return std::min<std::uint64_t>(130000ull, patchLeaseUs);
            case FootstepTruthGait::Walk:
            default:
                return std::min<std::uint64_t>(110000ull, patchLeaseUs);
            }
        }

        float FootstepProvisionalAmpScale(FootstepTruthGait gait)
        {
            switch (gait) {
            case FootstepTruthGait::Sprint:
                return 1.00f;
            case FootstepTruthGait::JumpUp:
            case FootstepTruthGait::JumpDown:
                return 1.04f;
            case FootstepTruthGait::Walk:
            default:
                return 0.96f;
            }
        }

        std::pair<std::uint8_t, std::uint8_t> ResolveFootstepStrideMotors(
            const HapticsConfig& cfg,
            std::uint8_t seedLeft,
            std::uint8_t seedRight,
            float ampScaleValue,
            float panSigned)
        {
            const auto seedLeftBase = (seedLeft != 0) ?
                seedLeft :
                static_cast<std::uint8_t>(std::clamp(cfg.basePulseFootstep * 255.0f, 0.0f, 255.0f));
            const auto seedRightBase = (seedRight != 0) ?
                seedRight :
                static_cast<std::uint8_t>(std::clamp(cfg.basePulseFootstep * 255.0f, 0.0f, 255.0f));

            const float ampScale = std::clamp(ampScaleValue, 0.90f, 1.24f);
            const float panMix = 0.20f;
            const float leftScale = std::clamp(
                ampScale * (1.0f - panSigned * panMix),
                0.75f,
                1.30f);
            const float rightScale = std::clamp(
                ampScale * (1.0f + panSigned * panMix),
                0.75f,
                1.30f);

            const auto left = static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(seedLeftBase) * leftScale,
                0.0f,
                255.0f));
            const auto right = static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(seedRightBase) * rightScale,
                0.0f,
                255.0f));
            return { left, right };
        }

        FootstepTexturePreset DefaultFootstepTexturePresetForGait(FootstepTruthGait gait)
        {
            switch (gait) {
            case FootstepTruthGait::Sprint:
                return FootstepTexturePreset::SprintDrive;
            case FootstepTruthGait::JumpUp:
                return FootstepTexturePreset::JumpLift;
            case FootstepTruthGait::JumpDown:
                return FootstepTexturePreset::LandSlam;
            case FootstepTruthGait::Walk:
            default:
                return FootstepTexturePreset::WalkSoft;
            }
        }

        float ClampUnit(float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        float SmoothStep(float edge0, float edge1, float x)
        {
            if (edge0 == edge1) {
                return (x >= edge1) ? 1.0f : 0.0f;
            }
            const float t = ClampUnit((x - edge0) / (edge1 - edge0));
            return t * t * (3.0f - 2.0f * t);
        }

        float TrianglePulse(float phase, float center, float halfWidth)
        {
            if (halfWidth <= 0.0f) {
                return 0.0f;
            }
            return ClampUnit(1.0f - std::fabs(phase - center) / halfWidth);
        }

        float WindowBand(float phase, float start, float end)
        {
            if (end <= start) {
                return 0.0f;
            }
            const float mid = (start + end) * 0.5f;
            return SmoothStep(start, mid, phase) * (1.0f - SmoothStep(mid, end, phase));
        }

        struct FootstepTextureRender
        {
            std::uint8_t left{ 0 };
            std::uint8_t right{ 0 };
            std::uint64_t effectiveEndUs{ 0 };
        };

        FootstepTextureRender RenderFootstepTexture(
            const HapticsConfig& cfg,
            std::uint64_t truthUs,
            std::uint64_t releaseEndUs,
            std::uint64_t targetEndUs,
            std::uint8_t seedLeft,
            std::uint8_t seedRight,
            FootstepTruthGait gait,
            FootstepTruthSide side,
            FootstepTexturePreset preset,
            float ampScale,
            float panSigned,
            std::uint16_t scorePermille,
            bool provisional,
            std::uint64_t nowUs)
        {
            const auto [baseLeft, baseRight] = ResolveFootstepStrideMotors(
                cfg,
                seedLeft,
                seedRight,
                ampScale,
                panSigned);

            const auto gaitPreset = (preset != FootstepTexturePreset::None) ?
                preset :
                DefaultFootstepTexturePresetForGait(gait);
            const auto nominalTextureSpanUs = [&]() -> std::uint64_t {
                switch (gaitPreset) {
                case FootstepTexturePreset::SprintDrive:
                    return 150000ull;
                case FootstepTexturePreset::JumpLift:
                    return 126000ull;
                case FootstepTexturePreset::LandSlam:
                    return 152000ull;
                case FootstepTexturePreset::WalkSoft:
                default:
                    return 112000ull;
                }
            }();

            const auto effectiveEndUs = std::max(
                std::max(releaseEndUs, targetEndUs),
                truthUs + nominalTextureSpanUs);
            if (truthUs == 0 || nowUs <= truthUs) {
                return { baseLeft, baseRight, effectiveEndUs };
            }

            const auto totalSpanUs = std::max<std::uint64_t>(effectiveEndUs - truthUs, 1ull);
            const auto elapsedUs = std::min<std::uint64_t>(nowUs - truthUs, totalSpanUs);
            const float phase = ClampUnit(static_cast<float>(elapsedUs) / static_cast<float>(totalSpanUs));
            const float detail = ClampUnit((static_cast<float>(scorePermille) - 520.0f) / 360.0f);
            const float definition = provisional ? (0.72f + 0.10f * detail) : (0.94f + 0.06f * detail);
            const float tail = ClampUnit(1.0f - phase);

            float leadEnv = 0.0f;
            float trailEnv = 0.0f;

            switch (gaitPreset) {
            case FootstepTexturePreset::SprintDrive: {
                const float bed = (0.18f + 0.05f * detail) * tail;
                const float impact = TrianglePulse(phase, 0.05f, 0.08f) * (0.98f + 0.12f * detail);
                const float rebound = TrianglePulse(phase, 0.18f, 0.11f) * (0.48f + 0.12f * detail);
                const float grit = WindowBand(phase, 0.06f, 0.62f) * tail * (0.24f + 0.04f * detail);
                leadEnv = bed + impact + rebound * 0.34f + grit * 0.72f;
                trailEnv = bed * 0.96f + impact * 0.86f + rebound * 0.56f + grit * 0.82f;
                break;
            }
            case FootstepTexturePreset::JumpLift: {
                const float bed = (0.09f + 0.03f * detail) * tail;
                const float push = TrianglePulse(phase, 0.16f, 0.16f) * (0.74f + 0.10f * detail);
                const float lift = WindowBand(phase, 0.18f, 0.78f) * tail * (0.22f + 0.05f * detail);
                leadEnv = bed + push + lift * 0.88f;
                trailEnv = bed * 0.92f + push * 0.92f + lift;
                break;
            }
            case FootstepTexturePreset::LandSlam: {
                const float bed = (0.20f + 0.04f * detail) * tail;
                const float slam = TrianglePulse(phase, 0.04f, 0.08f) * (1.08f + 0.10f * detail);
                const float aftershock = TrianglePulse(phase, 0.22f, 0.14f) * (0.46f + 0.08f * detail);
                const float rattle = WindowBand(phase, 0.05f, 0.54f) * tail * (0.26f + 0.04f * detail);
                leadEnv = bed + slam + aftershock * 0.54f + rattle;
                trailEnv = bed * 0.96f + slam * 0.94f + aftershock * 0.66f + rattle * 0.86f;
                break;
            }
            case FootstepTexturePreset::WalkSoft:
            default: {
                const float bed = (0.12f + 0.04f * detail) * tail;
                const float heel = TrianglePulse(phase, 0.08f, 0.11f) * (0.76f + 0.10f * detail);
                const float toe = TrianglePulse(phase, 0.28f, 0.15f) * (0.34f + 0.10f * detail);
                const float brush = WindowBand(phase, 0.14f, 0.78f) * tail * (0.17f + 0.03f * detail);
                leadEnv = bed + heel + toe * 0.24f + brush * 0.94f;
                trailEnv = bed * 0.94f + heel * 0.74f + toe * 0.44f + brush * 1.08f;
                break;
            }
            }

            leadEnv *= definition;
            trailEnv *= definition;

            float leftEnv = 0.0f;
            float rightEnv = 0.0f;
            switch (side) {
            case FootstepTruthSide::Left:
                leftEnv = leadEnv;
                rightEnv = trailEnv;
                break;
            case FootstepTruthSide::Right:
                leftEnv = trailEnv;
                rightEnv = leadEnv;
                break;
            case FootstepTruthSide::Unknown:
            default: {
                const float merged = (leadEnv + trailEnv) * 0.5f;
                leftEnv = merged;
                rightEnv = merged;
                break;
            }
            }

            const auto left = static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(baseLeft) * std::clamp(leftEnv, 0.0f, 1.35f),
                0.0f,
                255.0f));
            const auto right = static_cast<std::uint8_t>(std::clamp(
                static_cast<float>(baseRight) * std::clamp(rightEnv, 0.0f, 1.35f),
                0.0f,
                255.0f));
            return { left, right, effectiveEndUs };
        }

        bool ClassifyForegroundFrame(const HidFrame& frame, int prioritySwing, RouteReason& reason)
        {
            if (frame.leftMotor == 0 && frame.rightMotor == 0) {
                reason = RouteReason::ZeroFrame;
                return true;
            }
            if (frame.eventType != EventType::Unknown) {
                if (frame.eventType == EventType::Ambient ||
                    frame.eventType == EventType::Music ||
                    frame.eventType == EventType::UI) {
                    reason = RouteReason::BackgroundEvent;
                    return false;
                }
                reason = RouteReason::EventForeground;
                return true;
            }
            if (frame.foregroundHint) {
                reason = RouteReason::ForegroundHint;
                return true;
            }
            if (frame.priority >= static_cast<std::uint8_t>(std::max(0, prioritySwing))) {
                reason = RouteReason::PriorityForeground;
                return true;
            }
            reason = RouteReason::UnknownBackground;
            return false;
        }

        bool ShouldCarryOverwrittenFrame(const HidFrame& frame, int prioritySwing)
        {
            if (frame.leftMotor == 0 && frame.rightMotor == 0) {
                return false;
            }

            if (frame.eventType != EventType::Unknown) {
                return true;
            }

            if (frame.foregroundHint) {
                return true;
            }

            if (frame.priority >= static_cast<std::uint8_t>(std::max(0, prioritySwing))) {
                return true;
            }

            return frame.confidence >= 0.62f;
        }
    }

    HidOutput& HidOutput::GetSingleton()
    {
        static HidOutput instance;
        return instance;
    }

    std::string HidOutput::WideToUtf8(const wchar_t* wstr)
    {
        if (!wstr) {
            return "unknown";
        }

        const int needed = ::WideCharToMultiByte(
            CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);

        if (needed <= 1) {
            return "unknown";
        }

        std::string out(static_cast<std::size_t>(needed - 1), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0, wstr, -1, out.data(), needed, nullptr, nullptr);

        return out;
    }

    bool HidOutput::IsBackgroundEvent(EventType type)
    {
        return type == EventType::Ambient ||
            type == EventType::Music ||
            type == EventType::UI;
    }

    bool HidOutput::IsForegroundFrame(const HidFrame& frame, int prioritySwing)
    {
        RouteReason reason = RouteReason::EventForeground;
        return ClassifyForegroundFrame(frame, prioritySwing, reason);
    }

    void HidOutput::SetDevice(hid_device* device)
    {
        std::scoped_lock lock(_deviceMutex);

        if (device && !_device) {
            logger::info("[Haptics][HidOutput] Device connected");
        }
        else if (!device && _device) {
            logger::info("[Haptics][HidOutput] Device disconnected");
        }

        if (device != _device) {
            logger::info("[Haptics][HidOutput] SetDevice ptr=0x{:X}", reinterpret_cast<std::uintptr_t>(device));
        }
        _device = device;
    }

    bool HidOutput::IsConnected() const
    {
        std::scoped_lock lock(_deviceMutex);
        return _device != nullptr;
    }

    std::uint32_t HidOutput::GetReaderPollTimeoutMs(std::uint32_t fallbackMs) const
    {
        const auto fallback = std::clamp<std::uint32_t>(fallbackMs, 1u, 16u);
        const auto& cfg = HapticsConfig::GetSingleton();
        const auto nowUs = ToQPC(Now());
        if (cfg.enableStateTrackScheduler) {
            return GetReaderPollTimeoutMsStateTrack(fallback, cfg, nowUs);
        }
        const auto wakeGuardUs = std::max<std::uint64_t>(700ull, static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs / 2u));
        const auto deadlineWindowUs = static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs);

        std::uint64_t earliestTargetUs = 0;
        {
            std::scoped_lock lock(_stateMutex);
            auto inspect = [&](const OutputStateSlot& slot) {
                if (!slot.valid) {
                    return;
                }
                const auto targetUs = (slot.frame.qpcTarget != 0) ? slot.frame.qpcTarget : slot.frame.qpc;
                if (targetUs == 0) {
                    return;
                }
                if (earliestTargetUs == 0 || targetUs < earliestTargetUs) {
                    earliestTargetUs = targetUs;
                }
            };
            inspect(_fgState);
            inspect(_fgCarryState);
            inspect(_bgState);
            inspect(_bgCarryState);
        }

        if (earliestTargetUs == 0) {
            return fallback;
        }

        if (earliestTargetUs <= (nowUs + deadlineWindowUs + wakeGuardUs)) {
            return 1u;
        }

        const auto deltaUs = earliestTargetUs - nowUs;
        const auto waitUs = (deltaUs > wakeGuardUs) ? (deltaUs - wakeGuardUs) : 0ull;
        auto waitMs = static_cast<std::uint32_t>(waitUs / 1000ull);
        if (waitMs == 0) {
            waitMs = 1u;
        }
        return std::clamp<std::uint32_t>(waitMs, 1u, fallback);
    }

    bool HidOutput::EnqueueFrame(
        const HidFrame& frame,
        bool foreground,
        std::uint32_t capacity,
        const HapticsConfig& cfg)
    {
        if (capacity == 0) {
            if (foreground) {
                _txDropQueueFullFg.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                _txDropQueueFullBg.fetch_add(1, std::memory_order_relaxed);
            }
            return false;
        }

        std::scoped_lock lock(_queueMutex);
        const bool isZeroFrame = (frame.leftMotor == 0 && frame.rightMotor == 0);
        if (_lastSubmittedValid) {
            const bool sameSignal =
                (frame.leftMotor == _lastSubmittedLeft) &&
                (frame.rightMotor == _lastSubmittedRight) &&
                (frame.eventType == _lastSubmittedEvent);
            const auto deltaUs = (frame.qpc > _lastSubmittedQpc) ?
                (frame.qpc - _lastSubmittedQpc) :
                0ull;
            const auto minRepeatUs = isZeroFrame ?
                static_cast<std::uint64_t>(cfg.hidIdleRepeatIntervalUs) :
                static_cast<std::uint64_t>(cfg.hidMinRepeatIntervalUs);

            if (sameSignal && deltaUs < minRepeatUs) {
                _txSkippedRepeat.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            const bool wasNonZero = (_lastSubmittedLeft != 0 || _lastSubmittedRight != 0);
            if (cfg.hidStopClearsQueue && isZeroFrame && wasNonZero) {
                _fgQueue.clear();
                _bgQueue.clear();
                _bgDequeuedSinceFg = 0;
                _txQueueDepthFg.store(0, std::memory_order_relaxed);
                _txQueueDepthBg.store(0, std::memory_order_relaxed);
                _txStopFlushes.fetch_add(1, std::memory_order_relaxed);
            }
        }

        auto& q = foreground ? _fgQueue : _bgQueue;
        if (q.size() >= capacity) {
            q.pop_front();
            if (foreground) {
                _txDropQueueFullFg.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                _txDropQueueFullBg.fetch_add(1, std::memory_order_relaxed);
            }
        }

        q.push_back(frame);
        if (foreground) {
            _txQueuedFg.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            _txQueuedBg.fetch_add(1, std::memory_order_relaxed);
        }

        _lastSubmittedLeft = frame.leftMotor;
        _lastSubmittedRight = frame.rightMotor;
        _lastSubmittedEvent = frame.eventType;
        _lastSubmittedQpc = frame.qpc;
        _lastSubmittedValid = true;

        _txQueueDepthFg.store(static_cast<std::uint32_t>(_fgQueue.size()), std::memory_order_relaxed);
        _txQueueDepthBg.store(static_cast<std::uint32_t>(_bgQueue.size()), std::memory_order_relaxed);
        return true;
    }

    bool HidOutput::SubmitFrameNonBlocking(const HidFrame& frame)
    {
        {
            const auto& cfg = HapticsConfig::GetSingleton();
            if (cfg.enableStateTrackScheduler) {
                return SubmitFrameStateTrack(frame, cfg, ToQPC(Now()));
            }
        }

        static std::atomic<std::uint64_t> s_submitRepeatProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_submitRepeatProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_submitOverwriteProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_submitOverwriteProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_submitLeadProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_submitLeadProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_submitCarryProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_submitCarryProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_submitCarryExpireProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_submitCarryExpireProbeLines{ 0 };

        auto out = frame;
        const auto nowUs = ToQPC(Now());
        if (out.qpc == 0) {
            out.qpc = nowUs;
        }
        if (out.qpcTarget == 0) {
            out.qpcTarget = out.qpc;
        }
        if (out.seq == 0) {
            out.seq = _seqGen.fetch_add(1, std::memory_order_relaxed);
        }

        const auto& cfg = HapticsConfig::GetSingleton();
        RouteReason routeReason = RouteReason::EventForeground;
        const bool foreground = ClassifyForegroundFrame(out, cfg.prioritySwing, routeReason);
        const bool isZeroFrame = (out.leftMotor == 0 && out.rightMotor == 0);
        const auto submitLeadUs = (out.qpcTarget >= out.qpc) ? (out.qpcTarget - out.qpc) : 0ull;
        if (submitLeadUs > 3000ull &&
            ShouldEmitWindowedProbe(
                s_submitLeadProbeWindowUs,
                s_submitLeadProbeLines,
                nowUs,
                8)) {
            logger::info(
                "[Haptics][ProbeSubmit] site=submit/lead_anomaly branch=high_target_lead fg={} seq={} evt={} src={} hint={} prio={} conf={:.2f} motor={}/{} lead={}us age={}us target={} submitQpc={}",
                foreground ? 1 : 0,
                out.seq,
                ToString(out.eventType),
                static_cast<int>(out.sourceType),
                out.foregroundHint ? 1 : 0,
                static_cast<int>(out.priority),
                out.confidence,
                static_cast<int>(out.leftMotor),
                static_cast<int>(out.rightMotor),
                submitLeadUs,
                (nowUs > out.qpc) ? (nowUs - out.qpc) : 0ull,
                out.qpcTarget,
                out.qpc);
        }
        bool ok = true;
        bool overwriteProbe = false;
        std::uint64_t overwritePrevSeq = 0;
        EventType overwritePrevEvent = EventType::Unknown;
        SourceType overwritePrevSource = SourceType::AudioMod;
        bool overwritePrevHint = false;
        std::uint8_t overwritePrevPriority = 0;
        float overwritePrevConfidence = 0.0f;
        std::uint8_t overwritePrevLeft = 0;
        std::uint8_t overwritePrevRight = 0;
        std::uint64_t overwritePrevAheadUs = 0;
        std::uint64_t overwritePrevOverdueUs = 0;
        std::uint64_t overwritePrevAgeUs = 0;
        std::uint64_t overwritePrevExpireInUs = 0;
        std::uint64_t overwritePrevLeadUs = 0;
        {
            std::scoped_lock lock(_stateMutex);
            auto& slot = foreground ? _fgState : _bgState;
            auto& carrySlot = foreground ? _fgCarryState : _bgCarryState;

            if (_lastSubmittedValid) {
                const bool sameSignal =
                    (out.leftMotor == _lastSubmittedLeft) &&
                    (out.rightMotor == _lastSubmittedRight) &&
                    (out.eventType == _lastSubmittedEvent);
                const auto deltaUs = (out.qpc > _lastSubmittedQpc) ?
                    (out.qpc - _lastSubmittedQpc) :
                    0ull;
                const auto minRepeatUs = isZeroFrame ?
                    static_cast<std::uint64_t>(cfg.hidIdleRepeatIntervalUs) :
                    static_cast<std::uint64_t>(cfg.hidMinRepeatIntervalUs);
                if (sameSignal && deltaUs < minRepeatUs) {
                    _txSkippedRepeat.fetch_add(1, std::memory_order_relaxed);
                    if (ShouldEmitWindowedProbe(
                            s_submitRepeatProbeWindowUs,
                            s_submitRepeatProbeLines,
                            nowUs,
                            12)) {
                        logger::info(
                            "[Haptics][ProbeSubmit] site=submit/repeat_guard branch=same_signal_min_interval fg={} seq={} evt={} src={} hint={} prio={} conf={:.2f} motor={}/{} delta={}us minReq={}us qLead={}us",
                            foreground ? 1 : 0,
                            out.seq,
                            ToString(out.eventType),
                            static_cast<int>(out.sourceType),
                            out.foregroundHint ? 1 : 0,
                            static_cast<int>(out.priority),
                            out.confidence,
                            static_cast<int>(out.leftMotor),
                            static_cast<int>(out.rightMotor),
                            deltaUs,
                            minRepeatUs,
                            (out.qpcTarget >= out.qpc) ? (out.qpcTarget - out.qpc) : 0ull);
                    }
                    ok = false;
                }

                const bool wasNonZero = (_lastSubmittedLeft != 0 || _lastSubmittedRight != 0);
                if (cfg.hidStopClearsQueue && isZeroFrame && wasNonZero) {
                    _fgState = {};
                    _fgCarryState = {};
                    _bgState = {};
                    _bgCarryState = {};
                    _fgQueue.clear();
                    _bgQueue.clear();
                    _txStopFlushes.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (ok) {
                const bool overwrite = slot.valid && nowUs <= slot.expireUs;
                if (overwrite) {
                    const auto prevTargetUs = (slot.frame.qpcTarget != 0) ? slot.frame.qpcTarget : slot.frame.qpc;
                    overwriteProbe = true;
                    overwritePrevSeq = slot.frame.seq;
                    overwritePrevEvent = slot.frame.eventType;
                    overwritePrevSource = slot.frame.sourceType;
                    overwritePrevHint = slot.frame.foregroundHint;
                    overwritePrevPriority = slot.frame.priority;
                    overwritePrevConfidence = slot.frame.confidence;
                    overwritePrevLeft = slot.frame.leftMotor;
                    overwritePrevRight = slot.frame.rightMotor;
                    overwritePrevAheadUs = (prevTargetUs > nowUs) ? (prevTargetUs - nowUs) : 0ull;
                    overwritePrevOverdueUs = (nowUs > prevTargetUs) ? (nowUs - prevTargetUs) : 0ull;
                    overwritePrevAgeUs = (nowUs > slot.updatedUs) ? (nowUs - slot.updatedUs) : 0ull;
                    overwritePrevExpireInUs = (slot.expireUs > nowUs) ? (slot.expireUs - nowUs) : 0ull;
                    overwritePrevLeadUs = (slot.frame.qpcTarget >= slot.frame.qpc) ?
                        (slot.frame.qpcTarget - slot.frame.qpc) :
                        0ull;

                    if (ShouldCarryOverwrittenFrame(slot.frame, cfg.prioritySwing)) {
                        const bool hadCarryBefore = carrySlot.valid;
                        const auto carrySeqBefore = hadCarryBefore ? carrySlot.frame.seq : 0ull;
                        const auto carryTargetBefore = hadCarryBefore ?
                            ((carrySlot.frame.qpcTarget != 0) ? carrySlot.frame.qpcTarget : carrySlot.frame.qpc) :
                            0ull;
                        const auto carryOverBefore = (hadCarryBefore && nowUs > carryTargetBefore) ?
                            (nowUs - carryTargetBefore) :
                            0ull;
                        const auto carryLeadBefore = hadCarryBefore &&
                            (carrySlot.frame.qpcTarget >= carrySlot.frame.qpc) ?
                            (carrySlot.frame.qpcTarget - carrySlot.frame.qpc) :
                            0ull;
                        const auto carryExpireBefore = hadCarryBefore ? carrySlot.expireUs : 0ull;

                        if (carrySlot.valid && nowUs > carrySlot.expireUs) {
                            if (ShouldEmitWindowedProbe(
                                    s_submitCarryExpireProbeWindowUs,
                                    s_submitCarryExpireProbeLines,
                                    nowUs,
                                    8)) {
                                logger::info(
                                    "[Haptics][ProbeSubmit] site=submit/carry_expire_drop fg={} seq={} evt={} target={} over={}us expOver={}us lead={}us",
                                    foreground ? 1 : 0,
                                    carrySlot.frame.seq,
                                    ToString(carrySlot.frame.eventType),
                                    (carrySlot.frame.qpcTarget != 0) ? carrySlot.frame.qpcTarget : carrySlot.frame.qpc,
                                    ((carrySlot.frame.qpcTarget != 0 ? carrySlot.frame.qpcTarget : carrySlot.frame.qpc) < nowUs) ?
                                        (nowUs - (carrySlot.frame.qpcTarget != 0 ? carrySlot.frame.qpcTarget : carrySlot.frame.qpc)) :
                                        0ull,
                                    (nowUs > carrySlot.expireUs) ? (nowUs - carrySlot.expireUs) : 0ull,
                                    (carrySlot.frame.qpcTarget >= carrySlot.frame.qpc) ?
                                        (carrySlot.frame.qpcTarget - carrySlot.frame.qpc) :
                                        0ull);
                            }
                            carrySlot.valid = false;
                            _txStateExpiredDrop.fetch_add(1, std::memory_order_relaxed);
                        }
                        const auto slotTargetUs = (slot.frame.qpcTarget != 0) ? slot.frame.qpcTarget : slot.frame.qpc;
                        bool acceptNewCarry = true;
                        if (carrySlot.valid) {
                            const auto carryTargetUs = (carrySlot.frame.qpcTarget != 0) ?
                                carrySlot.frame.qpcTarget :
                                carrySlot.frame.qpc;
                            // Keep earlier-deadline carry to avoid replacing near-due payload.
                            if (carryTargetUs <= slotTargetUs) {
                                acceptNewCarry = false;
                            }
                        }

                        if (acceptNewCarry) {
                            if (carrySlot.valid) {
                                if (foreground) {
                                    _txStateCarryDropFg.fetch_add(1, std::memory_order_relaxed);
                                }
                                else {
                                    _txStateCarryDropBg.fetch_add(1, std::memory_order_relaxed);
                                }
                            }

                            carrySlot = slot;
                            carrySlot.updatedUs = nowUs;
                            const auto carryMinTtlUs = std::clamp<std::uint64_t>(
                                static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs) * 3ull + 2000ull,
                                4000ull,
                                15000ull);
                            carrySlot.expireUs = std::max(carrySlot.expireUs, nowUs + carryMinTtlUs);
                            carrySlot.foreground = foreground;
                            carrySlot.valid = true;

                            if (foreground) {
                                _txStateCarryQueuedFg.fetch_add(1, std::memory_order_relaxed);
                            }
                            else {
                                _txStateCarryQueuedBg.fetch_add(1, std::memory_order_relaxed);
                            }

                            if (ShouldEmitWindowedProbe(
                                    s_submitCarryProbeWindowUs,
                                    s_submitCarryProbeLines,
                                    nowUs,
                                    10)) {
                                logger::info(
                                    "[Haptics][ProbeSubmit] site=submit/carry_decision branch=accept fg={} reason={} primary(seq={} evt={} target={} over={}us) carryBefore(valid={} seq={} target={} over={}us) carryAfter(seq={} target={} ttl={}us)",
                                    foreground ? 1 : 0,
                                    hadCarryBefore ? "replace_later_deadline" : "capture_overwritten_primary",
                                    slot.frame.seq,
                                    ToString(slot.frame.eventType),
                                    slotTargetUs,
                                    (nowUs > slotTargetUs) ? (nowUs - slotTargetUs) : 0ull,
                                    hadCarryBefore ? 1 : 0,
                                    carrySeqBefore,
                                    carryTargetBefore,
                                    carryOverBefore,
                                    carrySlot.frame.seq,
                                    (carrySlot.frame.qpcTarget != 0) ? carrySlot.frame.qpcTarget : carrySlot.frame.qpc,
                                    (carrySlot.expireUs > nowUs) ? (carrySlot.expireUs - nowUs) : 0ull);
                            }
                        }
                        else {
                            if (foreground) {
                                _txStateCarryDropFg.fetch_add(1, std::memory_order_relaxed);
                            }
                            else {
                                _txStateCarryDropBg.fetch_add(1, std::memory_order_relaxed);
                            }

                            if (ShouldEmitWindowedProbe(
                                    s_submitCarryProbeWindowUs,
                                    s_submitCarryProbeLines,
                                    nowUs,
                                    10)) {
                                logger::info(
                                    "[Haptics][ProbeSubmit] site=submit/carry_decision branch=reject fg={} reason=keep_earlier_deadline primary(seq={} evt={} target={} over={}us) carryKeep(seq={} target={} over={}us)",
                                    foreground ? 1 : 0,
                                    slot.frame.seq,
                                    ToString(slot.frame.eventType),
                                    slotTargetUs,
                                    (nowUs > slotTargetUs) ? (nowUs - slotTargetUs) : 0ull,
                                    carrySeqBefore,
                                    carryTargetBefore,
                                    carryOverBefore);
                                logger::info(
                                    "[Haptics][ProbeSubmit] site=submit/carry_reject_detail fg={} rule=earlier_deadline primary(seq={} target={} over={}us) carry(seq={} target={} over={}us expIn={}us lead={}us) deadlineDelta={}us",
                                    foreground ? 1 : 0,
                                    slot.frame.seq,
                                    slotTargetUs,
                                    (nowUs > slotTargetUs) ? (nowUs - slotTargetUs) : 0ull,
                                    carrySeqBefore,
                                    carryTargetBefore,
                                    carryOverBefore,
                                    (carryExpireBefore > nowUs) ? (carryExpireBefore - nowUs) : 0ull,
                                    carryLeadBefore,
                                    (carryTargetBefore > slotTargetUs) ?
                                        (carryTargetBefore - slotTargetUs) :
                                        (slotTargetUs - carryTargetBefore));
                            }
                        }
                    }
                }
                slot.valid = true;
                slot.foreground = foreground;
                slot.frame = out;
                slot.updatedUs = nowUs;
                const auto baseTtlUs = isZeroFrame ?
                    std::max<std::uint64_t>(cfg.hidIdleRepeatIntervalUs, 12000ull) :
                    std::max<std::uint64_t>(cfg.hidStaleUs + 20000ull, 50000ull);
                slot.expireUs = nowUs + baseTtlUs;

                if (foreground) {
                    _txQueuedFg.fetch_add(1, std::memory_order_relaxed);
                    _txStateUpdateFg.fetch_add(1, std::memory_order_relaxed);
                    if (overwrite) {
                        _txStateOverwriteFg.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else {
                    _txQueuedBg.fetch_add(1, std::memory_order_relaxed);
                    _txStateUpdateBg.fetch_add(1, std::memory_order_relaxed);
                    if (overwrite) {
                        _txStateOverwriteBg.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }

            const std::uint32_t fgDepth = (_fgState.valid ? 1u : 0u) + (_fgCarryState.valid ? 1u : 0u);
            const std::uint32_t bgDepth = (_bgState.valid ? 1u : 0u) + (_bgCarryState.valid ? 1u : 0u);
            _txQueueDepthFg.store(fgDepth, std::memory_order_relaxed);
            _txQueueDepthBg.store(bgDepth, std::memory_order_relaxed);
        }

        if (ok && overwriteProbe &&
            ShouldEmitWindowedProbe(
                s_submitOverwriteProbeWindowUs,
                s_submitOverwriteProbeLines,
                nowUs,
                12)) {
            logger::info(
                "[Haptics][ProbeSubmit] site=submit/overwrite branch=state_slot_replace fg={} prev(seq={} evt={} src={} hint={} prio={} conf={:.2f} motor={}/{} ahead={}us over={}us age={}us expIn={}us lead={}us) next(seq={} evt={} src={} hint={} prio={} conf={:.2f} motor={}/{} lead={}us ttl={}us)",
                foreground ? 1 : 0,
                overwritePrevSeq,
                ToString(overwritePrevEvent),
                static_cast<int>(overwritePrevSource),
                overwritePrevHint ? 1 : 0,
                static_cast<int>(overwritePrevPriority),
                overwritePrevConfidence,
                static_cast<int>(overwritePrevLeft),
                static_cast<int>(overwritePrevRight),
                overwritePrevAheadUs,
                overwritePrevOverdueUs,
                overwritePrevAgeUs,
                overwritePrevExpireInUs,
                overwritePrevLeadUs,
                out.seq,
                ToString(out.eventType),
                static_cast<int>(out.sourceType),
                out.foregroundHint ? 1 : 0,
                static_cast<int>(out.priority),
                out.confidence,
                static_cast<int>(out.leftMotor),
                static_cast<int>(out.rightMotor),
                (out.qpcTarget >= out.qpc) ? (out.qpcTarget - out.qpc) : 0ull,
                isZeroFrame ?
                    std::max<std::uint64_t>(cfg.hidIdleRepeatIntervalUs, 12000ull) :
                    std::max<std::uint64_t>(cfg.hidStaleUs + 20000ull, 50000ull));
        }

        _lastSubmittedLeft = out.leftMotor;
        _lastSubmittedRight = out.rightMotor;
        _lastSubmittedEvent = out.eventType;
        _lastSubmittedQpc = out.qpc;
        _lastSubmittedValid = true;

        if (foreground) {
            _txRouteFg.fetch_add(1, std::memory_order_relaxed);
            switch (routeReason) {
            case RouteReason::ZeroFrame:
                _txRouteFgZero.fetch_add(1, std::memory_order_relaxed);
                break;
            case RouteReason::ForegroundHint:
                _txRouteFgHint.fetch_add(1, std::memory_order_relaxed);
                break;
            case RouteReason::PriorityForeground:
                _txRouteFgPriority.fetch_add(1, std::memory_order_relaxed);
                break;
            case RouteReason::EventForeground:
            default:
                _txRouteFgEvent.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        else {
            _txRouteBg.fetch_add(1, std::memory_order_relaxed);
            if (routeReason == RouteReason::UnknownBackground) {
                _txRouteBgUnknown.fetch_add(1, std::memory_order_relaxed);
            }
            else if (routeReason == RouteReason::BackgroundEvent) {
                _txRouteBgBackground.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Routing probes are already validated. Keep only periodic Tx stats and
        // focus detailed probes on flush-stage anomaly branches.
        (void)ok;

        return true;
    }

    bool HidOutput::SendFrame(const HidFrame& frame)
    {
        return SubmitFrameNonBlocking(frame);
    }

    void HidOutput::StopVibration()
    {
        logger::info("[Haptics][HidOutput] Stopping vibration...");
        HidFrame f{};
        f.qpc = ToQPC(Now());
        f.qpcTarget = f.qpc;
        f.foregroundHint = true;
        f.priority = 255;
        f.leftMotor = 0;
        f.rightMotor = 0;
        (void)SubmitFrameNonBlocking(f);
    }

    std::uint8_t HidOutput::TrackIndexForFrame(const HidFrame& frame, int prioritySwing)
    {
        switch (frame.eventType) {
        case EventType::HitImpact:
        case EventType::Block:
            return 0;  // Impact lane
        case EventType::WeaponSwing:
        case EventType::SpellCast:
        case EventType::SpellImpact:
        case EventType::BowRelease:
        case EventType::Shout:
            return 1;  // Swing lane
        case EventType::Footstep:
        case EventType::Jump:
        case EventType::Land:
            return 2;  // Footstep lane
        default:
            break;
        }

        if (frame.foregroundHint || frame.priority >= static_cast<std::uint8_t>(std::max(0, prioritySwing))) {
            return 1;
        }
        return 3;      // Utility lane
    }

    std::uint64_t HidOutput::TrackReleaseWindowUs(EventType type)
    {
        switch (type) {
        case EventType::HitImpact:
        case EventType::Block:
            return 70000ull;
        case EventType::WeaponSwing:
        case EventType::SpellCast:
        case EventType::SpellImpact:
        case EventType::BowRelease:
        case EventType::Shout:
            return 76000ull;
        case EventType::Footstep:
        case EventType::Jump:
        case EventType::Land:
            return 62000ull;
        default:
            return 42000ull;
        }
    }

    std::uint32_t HidOutput::GetReaderPollTimeoutMsStateTrack(
        std::uint32_t fallbackMs,
        const HapticsConfig& cfg,
        std::uint64_t nowUs) const
    {
        const auto fallback = std::clamp<std::uint32_t>(fallbackMs, 1u, 16u);
        const auto dueWindowUs = std::max<std::uint64_t>(
            static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs),
            static_cast<std::uint64_t>(cfg.stateTrackLookaheadMinUs));
        const auto wakeGuardUs = std::max<std::uint64_t>(700ull, dueWindowUs / 2ull);

        std::uint64_t earliestReadyUs = 0;
        {
            std::scoped_lock lock(_trackMutex);
            for (std::uint8_t i = 0; i < _trackStates.size(); ++i) {
                const auto& track = _trackStates[i];
                if (track.stopPending) {
                    earliestReadyUs = nowUs;
                    break;
                }
                const bool renderedLane = IsRenderedTrackLane(cfg, i);
                const bool cadenceLane = UsesCadenceLiveness(cfg, i);
                const bool activeValid =
                    track.active.valid &&
                    (track.active.releaseEndUs == 0 || nowUs < track.active.releaseEndUs);
                const bool pendingValid =
                    track.pending.valid &&
                    (track.pending.releaseEndUs == 0 || nowUs < track.pending.releaseEndUs);
                if (!activeValid && !pendingValid) {
                    continue;
                }

                const auto& slot = activeValid ? track.active : track.pending;
                const auto deadlineUs = (slot.deadlineUs != 0) ? slot.deadlineUs : slot.lastUpdateUs;
                if (deadlineUs == 0) {
                    continue;
                }

                const auto left = static_cast<std::uint8_t>(std::clamp(
                    static_cast<float>(slot.baseLeft), 0.0f, 255.0f));
                const auto right = static_cast<std::uint8_t>(std::clamp(
                    static_cast<float>(slot.baseRight), 0.0f, 255.0f));
                const bool silentFootstepSeed =
                    renderedLane &&
                    slot.eventType == EventType::Footstep &&
                    slot.sourceQpc != 0 &&
                    slot.structured.footstep.patchLeaseExpireUs != 0 &&
                    nowUs < slot.structured.footstep.patchLeaseExpireUs;
                const bool silentSessionPatchReady =
                    silentFootstepSeed &&
                    cfg.enableFootstepAudioModifierLivePatch &&
                    cfg.enableFootstepTruthSessionLivePatch &&
                    FootstepTruthSessionShadow::GetSingleton().HasLivePatchForTruth(
                        slot.sourceQpc,
                        slot.structured.footstep.modifier.revisionApplied,
                        nowUs);
                if (left == 0 && right == 0 && !silentSessionPatchReady) {
                    continue;
                }

                std::uint64_t readyUs = 0;
                if (renderedLane) {
                    if (silentSessionPatchReady) {
                        readyUs = nowUs;
                    } else if (activeValid) {
                        readyUs = (track.nextReadyUs != 0) ? track.nextReadyUs : deadlineUs;
                    } else {
                        readyUs = (track.nextReadyUs != 0) ?
                            std::max(deadlineUs, track.nextReadyUs) :
                            deadlineUs;
                    }
                }
                else {
                    readyUs = std::max(deadlineUs, track.nextReadyUs);
                }

                if (cadenceLane &&
                    activeValid &&
                    !pendingValid &&
                    track.livenessDeadlineUs != 0) {
                    readyUs = (readyUs == 0) ?
                        track.livenessDeadlineUs :
                        std::min(readyUs, track.livenessDeadlineUs);
                }

                if (earliestReadyUs == 0 || readyUs < earliestReadyUs) {
                    earliestReadyUs = readyUs;
                }
            }
        }

        if (earliestReadyUs == 0) {
            return fallback;
        }
        if (earliestReadyUs <= (nowUs + dueWindowUs + wakeGuardUs)) {
            return 1u;
        }

        const auto deltaUs = earliestReadyUs - nowUs;
        const auto waitUs = (deltaUs > wakeGuardUs) ? (deltaUs - wakeGuardUs) : 0ull;
        auto waitMs = static_cast<std::uint32_t>(waitUs / 1000ull);
        if (waitMs == 0) {
            waitMs = 1u;
        }
        return std::clamp<std::uint32_t>(waitMs, 1u, fallback);
    }

    bool HidOutput::SubmitFrameStateTrack(const HidFrame& frame, const HapticsConfig& cfg, std::uint64_t nowUs)
    {
        static std::atomic<std::uint64_t> s_trackSubmitProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackSubmitProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackSubmitRepeatProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackSubmitRepeatProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackStopProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackStopProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackPendingProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackPendingProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackSupersedeProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackSupersedeProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackAdmissionProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackAdmissionProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackTruthSuppressProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackTruthSuppressProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackShoutSuppressProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackShoutSuppressProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_attackSubmitProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_attackSubmitProbeLines{ 0 };

        HidFrame out = frame;
        if (out.qpc == 0) {
            out.qpc = nowUs;
        }
        if (out.qpcTarget == 0) {
            out.qpcTarget = out.qpc;
        }
        if (out.seq == 0) {
            out.seq = _seqGen.fetch_add(1, std::memory_order_relaxed);
        }

        RouteReason routeReason = RouteReason::EventForeground;
        const bool foreground = ClassifyForegroundFrame(out, cfg.prioritySwing, routeReason);
        const bool isZeroFrame = (out.leftMotor == 0 && out.rightMotor == 0);
        const auto trackIndex = TrackIndexForFrame(out, cfg.prioritySwing);
        const bool tokenLane = IsTokenTrackLane(cfg, trackIndex);
        const bool cadenceLane = UsesCadenceLiveness(cfg, trackIndex);
        const auto submitLeadUs = (out.qpcTarget >= out.qpc) ? (out.qpcTarget - out.qpc) : 0ull;
        const auto dueWindowUs = std::max<std::uint64_t>(
            static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs),
            static_cast<std::uint64_t>(cfg.stateTrackLookaheadMinUs));
        const bool footstepTruthSilentSeed =
            cfg.enableStateTrackFootstepTruthTrigger &&
            !cfg.enableStateTrackFootstepTruthAttack &&
            isZeroFrame &&
            out.eventType == EventType::Footstep &&
            out.sourceType == SourceType::BaseEvent;
        const bool structuredCombatEvent = IsStructuredCombatEvent(out.eventType);
        const bool truthFirstFootstepAudioSuppressed =
            cfg.enableStateTrackFootstepTruthTrigger &&
            out.eventType == EventType::Footstep &&
            out.sourceType == SourceType::AudioMod &&
            !isZeroFrame;
        const bool shoutLiveSuppressed =
            out.eventType == EventType::Shout &&
            !cfg.enableStateTrackShoutLiveOutput;

        if (truthFirstFootstepAudioSuppressed) {
            if (ShouldEmitWindowedProbe(
                    s_trackTruthSuppressProbeWindowUs,
                    s_trackTruthSuppressProbeLines,
                    nowUs,
                    8)) {
                logger::info(
                    "[Haptics][ProbeTrack] site=track/truth_audio_suppress lane={} evt={} src={} motor={}/{} conf={:.2f} prio={} lead={}us",
                    static_cast<int>(trackIndex),
                    ToString(out.eventType),
                    static_cast<int>(out.sourceType),
                    static_cast<int>(out.leftMotor),
                    static_cast<int>(out.rightMotor),
                    out.confidence,
                    static_cast<int>(out.priority),
                    submitLeadUs);
            }
            return true;
        }

        if (shoutLiveSuppressed) {
            if (ShouldEmitWindowedProbe(
                    s_trackShoutSuppressProbeWindowUs,
                    s_trackShoutSuppressProbeLines,
                    nowUs,
                    8)) {
                logger::info(
                    "[Haptics][ProbeTrack] site=track/shout_suppress lane={} evt={} src={} motor={}/{} conf={:.2f} prio={} lead={}us",
                    static_cast<int>(trackIndex),
                    ToString(out.eventType),
                    static_cast<int>(out.sourceType),
                    static_cast<int>(out.leftMotor),
                    static_cast<int>(out.rightMotor),
                    out.confidence,
                    static_cast<int>(out.priority),
                    submitLeadUs);
            }
            return true;
        }

        if (structuredCombatEvent &&
            ShouldEmitWindowedProbe(
                s_attackSubmitProbeWindowUs,
                s_attackSubmitProbeLines,
                nowUs,
                16)) {
            logger::info(
                "[Haptics][ProbeAttack] site=attack/submit lane={} evt={} fg={} route={} src={} motor={}/{} conf={:.2f} prio={} lead={}us dueWin={}us zero={} trackRendered={}",
                static_cast<int>(trackIndex),
                ToString(out.eventType),
                foreground ? 1 : 0,
                RouteReasonLabel(routeReason),
                static_cast<int>(out.sourceType),
                static_cast<int>(out.leftMotor),
                static_cast<int>(out.rightMotor),
                out.confidence,
                static_cast<int>(out.priority),
                submitLeadUs,
                dueWindowUs,
                isZeroFrame ? 1 : 0,
                IsRenderedTrackLane(cfg, trackIndex) ? 1 : 0);
        }

        const auto cadenceQuietWindowUsForGap =
            [&](std::uint64_t expectedGapUs, EventType eventType) -> std::uint64_t {
            const auto releaseUs = TrackReleaseWindowUs(eventType);
            const auto fallbackGapUs = std::max<std::uint64_t>(releaseUs / 3ull, 18000ull);
            const auto effectiveGapUs = (expectedGapUs != 0) ? expectedGapUs : fallbackGapUs;
            return std::clamp<std::uint64_t>(
                std::max(effectiveGapUs * 2ull, fallbackGapUs),
                18000ull,
                releaseUs);
        };

        const bool footstepAdmissionCandidate =
            tokenLane &&
            out.eventType == EventType::Footstep &&
            !isZeroFrame;
        if (footstepAdmissionCandidate) {
            auto& contextMgr = dualpad::input::ContextManager::GetSingleton();
            const auto currentContext = contextMgr.GetCurrentContext();
            const bool contextOk =
                !cfg.enableStateTrackFootstepContextGate ||
                contextMgr.IsFootstepContextAllowed();
            const auto recentMoveWindowUs =
                static_cast<std::uint64_t>(cfg.stateTrackFootstepRecentMoveMs) * 1000ull;
            const bool moving = contextMgr.IsPlayerMoving();
            const bool recentMove = contextMgr.WasPlayerMovingRecently(recentMoveWindowUs);
            const bool motionOk =
                !cfg.enableStateTrackFootstepMotionGate ||
                moving ||
                recentMove;
            if (!contextOk || !motionOk) {
                const auto lastMoveUs = contextMgr.GetLastPlayerMoveUs();
                const auto lastMoveAgoUs =
                    (lastMoveUs != 0 && nowUs >= lastMoveUs) ?
                    (nowUs - lastMoveUs) :
                    0ull;
                if (ShouldEmitWindowedProbe(
                        s_trackAdmissionProbeWindowUs,
                        s_trackAdmissionProbeLines,
                        nowUs,
                        10)) {
                    logger::info(
                        "[Haptics][ProbeTrack] site=track/admission_drop lane={} evt={} reason={}{} ctx={} moving={} recent={} recentWin={}us lastMoveAgo={}us motor={}/{} conf={:.2f} prio={} lead={}us",
                        static_cast<int>(trackIndex),
                        ToString(out.eventType),
                        contextOk ? "" : "context",
                        motionOk ? "" : (contextOk ? "motion" : "+motion"),
                        dualpad::input::ToString(currentContext),
                        moving ? 1 : 0,
                        recentMove ? 1 : 0,
                        recentMoveWindowUs,
                        lastMoveAgoUs,
                        static_cast<int>(out.leftMotor),
                        static_cast<int>(out.rightMotor),
                        out.confidence,
                        static_cast<int>(out.priority),
                        submitLeadUs);
                }
                return true;
            }
        }

        bool handledStructuredStop = false;
        if (isZeroFrame && !footstepTruthSilentSeed) {
            std::uint32_t fgDepth = 0;
            std::uint32_t bgDepth = 0;
            bool dropZeroNoop = false;
            bool queueStopIntent = false;
            bool activeValidBefore = false;
            bool pendingValidBefore = false;
            EventType activeEventBefore = EventType::Unknown;
            EventType pendingEventBefore = EventType::Unknown;
            std::uint64_t activeSeqBefore = 0;
            std::uint64_t pendingSeqBefore = 0;
            std::uint8_t activeLeftBefore = 0;
            std::uint8_t activeRightBefore = 0;
            std::uint8_t pendingLeftBefore = 0;
            std::uint8_t pendingRightBefore = 0;
            std::uint64_t activeReleaseRemainUs = 0;
            std::uint64_t pendingReleaseRemainUs = 0;
            std::uint64_t nextReadyDeltaUs = 0;
            bool structuredZeroIntent = false;
            {
                std::scoped_lock lock(_trackMutex);
                auto& track = _trackStates[trackIndex];

                auto pruneExpired = [&](TrackSlotState& slot) {
                    if (slot.valid &&
                        slot.releaseEndUs != 0 &&
                        nowUs >= slot.releaseEndUs) {
                        slot = TrackSlotState{};
                    }
                };
                auto slotCarriesState = [&](const TrackSlotState& slot) {
                    if (!slot.valid) {
                        return false;
                    }
                    if (slot.releaseEndUs != 0 && nowUs >= slot.releaseEndUs) {
                        return false;
                    }
                    return
                        slot.baseLeft != 0 ||
                        slot.baseRight != 0 ||
                        slot.eventType != EventType::Unknown ||
                        slot.hint ||
                        slot.priority != 0 ||
                        slot.confidence > 0.01f;
                };

                pruneExpired(track.active);
                pruneExpired(track.pending);

                const bool laneHasMeaningfulState =
                    slotCarriesState(track.active) ||
                    slotCarriesState(track.pending);
                structuredZeroIntent =
                    out.eventType != EventType::Unknown ||
                    out.foregroundHint ||
                    out.priority != 0 ||
                    out.confidence > 0.05f;

                activeValidBefore = track.active.valid;
                pendingValidBefore = track.pending.valid;
                activeEventBefore = track.active.eventType;
                pendingEventBefore = track.pending.eventType;
                activeSeqBefore = track.active.seq;
                pendingSeqBefore = track.pending.seq;
                activeLeftBefore = track.active.baseLeft;
                activeRightBefore = track.active.baseRight;
                pendingLeftBefore = track.pending.baseLeft;
                pendingRightBefore = track.pending.baseRight;
                activeReleaseRemainUs = (track.active.releaseEndUs > nowUs) ?
                    (track.active.releaseEndUs - nowUs) :
                    0ull;
                pendingReleaseRemainUs = (track.pending.releaseEndUs > nowUs) ?
                    (track.pending.releaseEndUs - nowUs) :
                    0ull;
                nextReadyDeltaUs = (track.nextReadyUs > nowUs) ?
                    (track.nextReadyUs - nowUs) :
                    0ull;

                if (cadenceLane) {
                    dropZeroNoop = true;
                    queueStopIntent = false;
                }

                queueStopIntent =
                    !cadenceLane &&
                    cfg.hidStopClearsQueue &&
                    structuredZeroIntent &&
                    laneHasMeaningfulState;
                dropZeroNoop = !queueStopIntent;

                if (queueStopIntent) {
                    const auto stopType = activeValidBefore ?
                        activeEventBefore :
                        pendingEventBefore;
                    const auto stopWindowUs = std::clamp<std::uint64_t>(
                        TrackReleaseWindowUs(stopType) / 4ull,
                        10000ull,
                        26000ull);
                    const bool stopAlreadyQueued = track.stopPending;
                    const bool alreadyInShortRelease =
                        activeValidBefore &&
                        activeReleaseRemainUs != 0 &&
                        activeReleaseRemainUs <= (stopWindowUs + 2000ull);

                    if (stopAlreadyQueued || alreadyInShortRelease) {
                        queueStopIntent = false;
                        dropZeroNoop = true;
                    } else {
                        track.stopPending = true;
                        track.stopRequestedUs = nowUs;
                        track.stopSeq = out.seq;
                        track.stopEventType = out.eventType;
                        for (const auto& lane : _trackStates) {
                            const bool laneValid = lane.active.valid || lane.pending.valid;
                            if (!laneValid) {
                                continue;
                            }
                            const bool laneForeground = lane.active.valid ?
                                lane.active.foreground :
                                lane.pending.foreground;
                            if (laneForeground) {
                                ++fgDepth;
                            }
                            else {
                                ++bgDepth;
                            }
                        }
                    }
                }
            }

            if (dropZeroNoop) {
                return true;
            }
            if (queueStopIntent) {
                _txQueueDepthFg.store(fgDepth, std::memory_order_relaxed);
                _txQueueDepthBg.store(bgDepth, std::memory_order_relaxed);
                handledStructuredStop = true;
                if (ShouldEmitWindowedProbe(
                        s_trackStopProbeWindowUs,
                        s_trackStopProbeLines,
                        nowUs,
                        8)) {
                    logger::info(
                        "[Haptics][ProbeTrack] site=track/zero_stop lane={} evt={} action=queue_stop structured={} active(valid={} seq={} evt={} motor={}/{} relRemain={}us) pending(valid={} seq={} evt={} motor={}/{} relRemain={}us) nextReady={}us",
                        static_cast<int>(trackIndex),
                        ToString(out.eventType),
                        structuredZeroIntent ? 1 : 0,
                        activeValidBefore ? 1 : 0,
                        activeSeqBefore,
                        ToString(activeEventBefore),
                        static_cast<int>(activeLeftBefore),
                        static_cast<int>(activeRightBefore),
                        activeReleaseRemainUs,
                        pendingValidBefore ? 1 : 0,
                        pendingSeqBefore,
                        ToString(pendingEventBefore),
                        static_cast<int>(pendingLeftBefore),
                        static_cast<int>(pendingRightBefore),
                        pendingReleaseRemainUs,
                        nextReadyDeltaUs);
                }
            }
        }

        if (_lastSubmittedValid) {
            const bool sameSignal =
                !footstepTruthSilentSeed &&
                (out.leftMotor == _lastSubmittedLeft) &&
                (out.rightMotor == _lastSubmittedRight) &&
                (out.eventType == _lastSubmittedEvent);
            const auto deltaUs = (out.qpc > _lastSubmittedQpc) ? (out.qpc - _lastSubmittedQpc) : 0ull;
            const auto minRepeatUs = isZeroFrame ?
                static_cast<std::uint64_t>(cfg.hidIdleRepeatIntervalUs) :
                static_cast<std::uint64_t>(cfg.hidMinRepeatIntervalUs);
            if (sameSignal && deltaUs < minRepeatUs) {
                const bool renderedLane = IsRenderedTrackLane(cfg, trackIndex);
                _txSkippedRepeat.fetch_add(1, std::memory_order_relaxed);
                if (ShouldEmitWindowedProbe(
                        s_trackSubmitRepeatProbeWindowUs,
                        s_trackSubmitRepeatProbeLines,
                        nowUs,
                        10)) {
                    logger::info(
                        "[Haptics][ProbeTrack] site=track/submit_repeat lane={} evt={} action={} motor={}/{} delta={}us minReq={}us lead={}us conf={:.2f} prio={}",
                        static_cast<int>(trackIndex),
                        ToString(out.eventType),
                        renderedLane ? "merge_intent" : "drop_submit",
                        static_cast<int>(out.leftMotor),
                        static_cast<int>(out.rightMotor),
                        deltaUs,
                        minRepeatUs,
                        submitLeadUs,
                        out.confidence,
                        static_cast<int>(out.priority));
                }
                if (!renderedLane) {
                    return true;
                }
            }
        }

        if (!handledStructuredStop) {
            std::uint32_t fgDepth = 0;
            std::uint32_t bgDepth = 0;
            const auto incomingDeadlineUs =
                (out.qpcTarget != 0) ? out.qpcTarget : (nowUs + dueWindowUs);
            {
                std::scoped_lock lock(_trackMutex);
                auto& track = _trackStates[trackIndex];
                const bool hadActiveBefore = track.active.valid;
                const bool hadPendingBefore = track.pending.valid;
                const auto activeBefore = track.active;
                const auto pendingBefore = track.pending;
                bool mergedPending = false;
                bool armedSupersede = false;
                std::uint64_t supersedeReadyInUs = 0;
                std::uint64_t activeReleaseRemainBeforeUs = 0;

                auto releaseWindowForEvent = [&](EventType type) -> std::uint64_t {
                    switch (type) {
                    case EventType::HitImpact:
                    case EventType::Block:
                        return cfg.stateTrackReleaseHitUs;
                    case EventType::WeaponSwing:
                    case EventType::SpellCast:
                    case EventType::SpellImpact:
                    case EventType::BowRelease:
                    case EventType::Shout:
                        return cfg.stateTrackReleaseSwingUs;
                    case EventType::Footstep:
                    case EventType::Jump:
                    case EventType::Land:
                        return cfg.stateTrackReleaseFootstepUs;
                    default:
                        return cfg.stateTrackReleaseUtilityUs;
                    }
                };

                auto mergeIntoSlot = [&](TrackSlotState& slot, bool decayExisting, bool assignNewEpoch) {
                    const bool hadValid = slot.valid;
                    const auto prevPriority = slot.priority;
                    const auto prevConfidence = slot.confidence;
                    const auto prevHint = slot.hint;
                    const auto prevBaseLeft = slot.baseLeft;
                    const auto prevBaseRight = slot.baseRight;
                    const auto prevSourceQpc = slot.sourceQpc;
                    const auto prevLastUpdateUs = slot.lastUpdateUs;
                    const auto prevReleaseEndUs = slot.releaseEndUs;
                    const auto prevPatchLeaseExpireUs = slot.structured.footstep.patchLeaseExpireUs;
                    const auto prevFootstepSeedLeft = slot.structured.footstep.seedLeft;
                    const auto prevFootstepSeedRight = slot.structured.footstep.seedRight;
                    const auto prevFootstepGait = slot.structured.footstep.gait;
                    const auto prevFootstepSide = slot.structured.footstep.side;
                    const auto prevFootstepTexturePreset = slot.structured.footstep.texturePreset;
                    const auto prevFootstepModifierAmpScale = slot.structured.footstep.modifier.ampScale;
                    const auto prevFootstepModifierPanSigned = slot.structured.footstep.modifier.panSigned;
                    const auto prevFootstepModifierScorePermille = slot.structured.footstep.modifier.scorePermille;
                    const auto prevFootstepTargetEndUs = slot.structured.footstep.modifier.targetEndUs;
                    const auto prevFootstepModifierProvisional = slot.structured.footstep.modifier.provisional;
                    const auto prevFootstepSessionRevisionApplied = slot.structured.footstep.modifier.revisionApplied;
                    const auto prevEpoch = slot.epoch;
                    const auto prevSupersededAtUs = slot.supersededAtUs;

                    slot.valid = true;
                    slot.foreground = foreground;
                    slot.eventType = out.eventType;
                    slot.sourceType = out.sourceType;
                    slot.priority = hadValid ? std::max(prevPriority, out.priority) : out.priority;
                    slot.confidence = hadValid ? std::max(prevConfidence, out.confidence) : out.confidence;
                    slot.hint = hadValid ? (prevHint || out.foregroundHint) : out.foregroundHint;
                    slot.sourceQpc = (hadValid && prevSourceQpc != 0) ?
                        std::min(prevSourceQpc, out.qpc) :
                        out.qpc;
                    slot.deadlineUs = std::max(incomingDeadlineUs, track.nextReadyUs);
                    slot.lastUpdateUs = nowUs;
                    slot.seq = out.seq;
                    slot.renderedOnce = hadValid ? slot.renderedOnce : false;
                    slot.epoch = (hadValid && !assignNewEpoch) ? prevEpoch : track.nextEpoch++;
                    slot.supersededAtUs = (hadValid && !assignNewEpoch) ? prevSupersededAtUs : 0;

                    if (hadValid) {
                        if (decayExisting) {
                            const auto ageUs = (nowUs > prevLastUpdateUs) ? (nowUs - prevLastUpdateUs) : 0ull;
                            const float decay = (ageUs >= 90000ull) ? 0.72f :
                                (ageUs >= 45000ull) ? 0.84f :
                                0.94f;
                            const float keptLeft = static_cast<float>(prevBaseLeft) * decay;
                            const float keptRight = static_cast<float>(prevBaseRight) * decay;
                            slot.baseLeft = static_cast<std::uint8_t>(std::clamp(
                                std::max(keptLeft, static_cast<float>(out.leftMotor)), 0.0f, 255.0f));
                            slot.baseRight = static_cast<std::uint8_t>(std::clamp(
                                std::max(keptRight, static_cast<float>(out.rightMotor)), 0.0f, 255.0f));
                        }
                        else {
                            slot.baseLeft = std::max(prevBaseLeft, out.leftMotor);
                            slot.baseRight = std::max(prevBaseRight, out.rightMotor);
                        }
                    }
                    else {
                        slot.baseLeft = out.leftMotor;
                        slot.baseRight = out.rightMotor;
                    }

                    const auto releaseAnchorUs = std::max(slot.deadlineUs, track.nextReadyUs);
                    const auto releaseWindowUs = releaseWindowForEvent(slot.eventType);
                    const auto releaseEndUs =
                        (slot.eventType == EventType::Footstep && slot.sourceQpc != 0) ?
                            (slot.sourceQpc + releaseWindowUs) :
                            (releaseAnchorUs + releaseWindowUs);
                    slot.releaseEndUs = hadValid ? std::max(prevReleaseEndUs, releaseEndUs) : releaseEndUs;
                    if (slot.eventType == EventType::Footstep && slot.sourceQpc != 0) {
                        const auto leaseEndUs = slot.sourceQpc + static_cast<std::uint64_t>(cfg.stateTrackFootstepPatchLeaseUs);
                        const auto defaultLeaseUs = static_cast<std::uint64_t>(
                            FootstepPatchLeaseUs(cfg, out.footstepGait));
                        const auto defaultTargetEndUs = slot.sourceQpc + FootstepTargetEndDeltaUs(cfg, out.footstepGait);
                        const auto leaseExpireUs = slot.sourceQpc + defaultLeaseUs;
                        slot.structured.footstep.patchLeaseExpireUs = hadValid ?
                            std::max(prevPatchLeaseExpireUs, std::max(leaseEndUs, leaseExpireUs)) :
                            std::max(leaseEndUs, leaseExpireUs);
                        if (hadValid) {
                            slot.structured.footstep.seedLeft = prevFootstepSeedLeft;
                            slot.structured.footstep.seedRight = prevFootstepSeedRight;
                            slot.structured.renderer = StructuredRendererKind::FootstepStride;
                            slot.structured.footstep.gait = (out.footstepGait != FootstepTruthGait::Unknown) ?
                                out.footstepGait :
                                prevFootstepGait;
                            slot.structured.footstep.side = (out.footstepSide != FootstepTruthSide::Unknown) ?
                                out.footstepSide :
                                prevFootstepSide;
                            slot.structured.footstep.texturePreset = (slot.structured.footstep.gait != prevFootstepGait ||
                                    prevFootstepTexturePreset == FootstepTexturePreset::None) ?
                                DefaultFootstepTexturePresetForGait(slot.structured.footstep.gait) :
                                prevFootstepTexturePreset;
                            slot.structured.footstep.modifier.ampScale = std::max(
                                prevFootstepModifierAmpScale,
                                FootstepProvisionalAmpScale(slot.structured.footstep.gait));
                            slot.structured.footstep.modifier.panSigned = prevFootstepModifierPanSigned;
                            slot.structured.footstep.modifier.scorePermille = std::max<std::uint16_t>(
                                prevFootstepModifierScorePermille,
                                static_cast<std::uint16_t>(prevFootstepModifierProvisional ? 560 : 620));
                            slot.structured.footstep.modifier.targetEndUs = std::max(prevFootstepTargetEndUs, defaultTargetEndUs);
                            slot.structured.footstep.modifier.provisional = prevFootstepModifierProvisional;
                            slot.structured.footstep.modifier.revisionApplied = prevFootstepSessionRevisionApplied;
                        } else {
                            const auto seedBase = static_cast<std::uint8_t>(std::clamp(
                                cfg.basePulseFootstep * 255.0f,
                                0.0f,
                                255.0f));
                            slot.structured.footstep.seedLeft = (out.leftMotor != 0) ? out.leftMotor : seedBase;
                            slot.structured.footstep.seedRight = (out.rightMotor != 0) ? out.rightMotor : seedBase;
                            slot.structured.renderer = StructuredRendererKind::FootstepStride;
                            slot.structured.footstep.gait = out.footstepGait;
                            slot.structured.footstep.side = out.footstepSide;
                            slot.structured.footstep.texturePreset = DefaultFootstepTexturePresetForGait(out.footstepGait);
                            slot.structured.footstep.modifier.ampScale = FootstepProvisionalAmpScale(out.footstepGait);
                            slot.structured.footstep.modifier.panSigned = 0.0f;
                            slot.structured.footstep.modifier.scorePermille = 560;
                            slot.structured.footstep.modifier.targetEndUs = defaultTargetEndUs;
                            slot.structured.footstep.modifier.provisional = true;
                            slot.structured.footstep.modifier.revisionApplied = 0;
                        }
                        slot.releaseEndUs = std::max(slot.releaseEndUs, slot.structured.footstep.modifier.targetEndUs);
                    }
                    else {
                        slot.structured.renderer = StructuredRendererKind::None;
                        slot.structured.footstep.patchLeaseExpireUs = 0;
                        slot.structured.footstep.seedLeft = 0;
                        slot.structured.footstep.seedRight = 0;
                        slot.structured.footstep.gait = FootstepTruthGait::Unknown;
                        slot.structured.footstep.side = FootstepTruthSide::Unknown;
                        slot.structured.footstep.texturePreset = FootstepTexturePreset::None;
                        slot.structured.footstep.modifier.ampScale = 1.0f;
                        slot.structured.footstep.modifier.panSigned = 0.0f;
                        slot.structured.footstep.modifier.scorePermille = 0;
                        slot.structured.footstep.modifier.targetEndUs = 0;
                        slot.structured.footstep.modifier.provisional = false;
                        slot.structured.footstep.modifier.revisionApplied = 0;
                    }
                };

                auto promotePendingToActive = [&]() {
                    if (!track.pending.valid) {
                        return false;
                    }
                    if (track.nextReadyUs != 0 && nowUs < track.nextReadyUs) {
                        return false;
                    }
                    track.active = track.pending;
                    track.pending = TrackSlotState{};
                    return true;
                };

                auto sameStrideSprintUpgrade = [&](TrackSlotState& slot) {
                    if (!tokenLane ||
                        !slot.valid ||
                        slot.eventType != EventType::Footstep ||
                        out.eventType != EventType::Footstep ||
                        out.footstepGait != FootstepTruthGait::Sprint ||
                        slot.structured.footstep.gait == FootstepTruthGait::Sprint ||
                        slot.structured.footstep.side == FootstepTruthSide::Unknown ||
                        out.footstepSide == FootstepTruthSide::Unknown ||
                        slot.structured.footstep.side != out.footstepSide ||
                        !SameStrideFamily(slot.structured.footstep.gait, out.footstepGait)) {
                        return false;
                    }

                    const auto strideDeltaUs = (slot.sourceQpc != 0 && nowUs >= slot.sourceQpc) ?
                        (nowUs - slot.sourceQpc) :
                        0ull;
                    if (strideDeltaUs > kFootstepStrideUpgradeCoalesceUs) {
                        return false;
                    }

                    mergeIntoSlot(slot, false, false);
                    track.nextReadyUs = nowUs;
                    return true;
                };

                const auto leaseKeepsSlotAlive = [&](const TrackSlotState& slot) {
                    return slot.valid &&
                        slot.eventType == EventType::Footstep &&
                        slot.structured.footstep.patchLeaseExpireUs != 0 &&
                        nowUs < slot.structured.footstep.patchLeaseExpireUs;
                };

                if (track.active.valid &&
                    track.active.releaseEndUs != 0 &&
                    nowUs >= track.active.releaseEndUs &&
                    !leaseKeepsSlotAlive(track.active)) {
                    track.active = TrackSlotState{};
                }
                if (track.pending.valid &&
                    track.pending.releaseEndUs != 0 &&
                    nowUs >= track.pending.releaseEndUs &&
                    !leaseKeepsSlotAlive(track.pending)) {
                    track.pending = TrackSlotState{};
                }
                if (!track.active.valid && !track.pending.valid) {
                    track.nextReadyUs = 0;
                    track.livenessDeadlineUs = 0;
                    track.cadenceLastTokenUs = 0;
                }

                if (cadenceLane) {
                    if (track.cadenceLastTokenUs != 0 && nowUs > track.cadenceLastTokenUs) {
                        const auto observedGapUs = nowUs - track.cadenceLastTokenUs;
                        if (observedGapUs >= 2000ull &&
                            observedGapUs <= TrackReleaseWindowUs(out.eventType)) {
                            track.cadenceExpectedGapUs =
                                (track.cadenceExpectedGapUs == 0) ?
                                    observedGapUs :
                                    ((track.cadenceExpectedGapUs * 3ull) + observedGapUs) / 4ull;
                        }
                    }
                    track.cadenceLastTokenUs = nowUs;
                    track.livenessDeadlineUs =
                        nowUs + cadenceQuietWindowUsForGap(track.cadenceExpectedGapUs, out.eventType);
                    track.stopPending = false;
                    track.stopRequestedUs = 0;
                    track.stopSeq = 0;
                    track.stopEventType = EventType::Unknown;
                }

                const bool laneCoolingDown = (track.nextReadyUs != 0 && nowUs < track.nextReadyUs);
                if (sameStrideSprintUpgrade(track.active)) {
                    mergedPending = false;
                }
                else if (sameStrideSprintUpgrade(track.pending)) {
                    mergedPending = true;
                }
                else if (!track.active.valid && !laneCoolingDown) {
                    (void)promotePendingToActive();
                }

                if (!track.active.valid && !laneCoolingDown) {
                    mergeIntoSlot(track.active, false, true);
                }
                else if (tokenLane && track.active.valid && !track.active.renderedOnce) {
                    mergeIntoSlot(track.active, false, false);
                }
                else {
                    mergeIntoSlot(track.pending, false, !hadPendingBefore);
                    mergedPending = true;
                    if (!tokenLane &&
                        IsRenderedTrackLane(cfg, trackIndex) &&
                        track.active.valid &&
                        track.pending.valid &&
                        track.pending.epoch > track.active.epoch &&
                        track.active.supersededAtUs == 0) {
                        activeReleaseRemainBeforeUs = (track.active.releaseEndUs > nowUs) ?
                            (track.active.releaseEndUs - nowUs) :
                            0ull;
                        track.active.supersededAtUs = nowUs;
                        const auto supersedeGraceUs = std::clamp<std::uint64_t>(
                            releaseWindowForEvent(track.active.eventType) / 6ull,
                            8000ull,
                            20000ull);
                        const auto switchReadyUs = std::max(track.nextReadyUs, nowUs + supersedeGraceUs);
                        supersedeReadyInUs = (switchReadyUs > nowUs) ? (switchReadyUs - nowUs) : 0ull;
                        armedSupersede = true;
                    }
                }

                for (const auto& t : _trackStates) {
                    const bool laneValid = t.active.valid || t.pending.valid;
                    if (!laneValid) {
                        continue;
                    }
                    const bool laneForeground = t.active.valid ? t.active.foreground : t.pending.foreground;
                    if (laneForeground) {
                        ++fgDepth;
                    }
                    else {
                        ++bgDepth;
                    }
                }

                if (foreground) {
                    _txQueuedFg.fetch_add(1, std::memory_order_relaxed);
                    _txStateUpdateFg.fetch_add(1, std::memory_order_relaxed);
                    if (hadActiveBefore || hadPendingBefore) {
                        _txStateOverwriteFg.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else {
                    _txQueuedBg.fetch_add(1, std::memory_order_relaxed);
                    _txStateUpdateBg.fetch_add(1, std::memory_order_relaxed);
                    if (hadActiveBefore || hadPendingBefore) {
                        _txStateOverwriteBg.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                const bool shouldLogPendingMerge =
                    mergedPending &&
                    (trackIndex == 2 || hadPendingBefore || laneCoolingDown);
                if (shouldLogPendingMerge &&
                    ShouldEmitWindowedProbe(
                        s_trackPendingProbeWindowUs,
                        s_trackPendingProbeLines,
                        nowUs,
                        10)) {
                    logger::info(
                        "[Haptics][ProbeTrack] site=track/pending_merge lane={} evt={} seq={} cooling={} activeBefore(valid={} seq={} evt={} rendered={} relRemain={}us) pendingBefore(valid={} seq={} evt={} motor={}/{} relRemain={}us) pendingAfter(seq={} evt={} motor={}/{} epoch={} nextReady={}us lead={}us)",
                        static_cast<int>(trackIndex),
                        ToString(out.eventType),
                        out.seq,
                        laneCoolingDown ? 1 : 0,
                        activeBefore.valid ? 1 : 0,
                        activeBefore.seq,
                        ToString(activeBefore.eventType),
                        activeBefore.renderedOnce ? 1 : 0,
                        (activeBefore.releaseEndUs > nowUs) ? (activeBefore.releaseEndUs - nowUs) : 0ull,
                        pendingBefore.valid ? 1 : 0,
                        pendingBefore.seq,
                        ToString(pendingBefore.eventType),
                        static_cast<int>(pendingBefore.baseLeft),
                        static_cast<int>(pendingBefore.baseRight),
                        (pendingBefore.releaseEndUs > nowUs) ? (pendingBefore.releaseEndUs - nowUs) : 0ull,
                        track.pending.seq,
                        ToString(track.pending.eventType),
                        static_cast<int>(track.pending.baseLeft),
                        static_cast<int>(track.pending.baseRight),
                        track.pending.epoch,
                        (track.nextReadyUs > nowUs) ? (track.nextReadyUs - nowUs) : 0ull,
                        submitLeadUs);
                }

                if (armedSupersede &&
                    ShouldEmitWindowedProbe(
                        s_trackSupersedeProbeWindowUs,
                        s_trackSupersedeProbeLines,
                        nowUs,
                        8)) {
                    logger::info(
                        "[Haptics][ProbeTrack] site=track/supersede_arm lane={} active(seq={} evt={} rendered={} relRemain={}us) pending(seq={} evt={} epoch={}) switchAfter={}us nextReady={}us",
                        static_cast<int>(trackIndex),
                        track.active.seq,
                        ToString(track.active.eventType),
                        track.active.renderedOnce ? 1 : 0,
                        activeReleaseRemainBeforeUs,
                        track.pending.seq,
                        ToString(track.pending.eventType),
                        track.pending.epoch,
                        supersedeReadyInUs,
                        (track.nextReadyUs > nowUs) ? (track.nextReadyUs - nowUs) : 0ull);
                }
            }
            _txQueueDepthFg.store(fgDepth, std::memory_order_relaxed);
            _txQueueDepthBg.store(bgDepth, std::memory_order_relaxed);
            _txTrackUpdate.fetch_add(1, std::memory_order_relaxed);
        }

        _lastSubmittedLeft = out.leftMotor;
        _lastSubmittedRight = out.rightMotor;
        _lastSubmittedEvent = out.eventType;
        _lastSubmittedQpc = out.qpc;
        _lastSubmittedValid = true;

        if (foreground) {
            _txRouteFg.fetch_add(1, std::memory_order_relaxed);
            switch (routeReason) {
            case RouteReason::ZeroFrame:
                _txRouteFgZero.fetch_add(1, std::memory_order_relaxed);
                break;
            case RouteReason::ForegroundHint:
                _txRouteFgHint.fetch_add(1, std::memory_order_relaxed);
                break;
            case RouteReason::PriorityForeground:
                _txRouteFgPriority.fetch_add(1, std::memory_order_relaxed);
                break;
            case RouteReason::EventForeground:
            default:
                _txRouteFgEvent.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        else {
            _txRouteBg.fetch_add(1, std::memory_order_relaxed);
            if (routeReason == RouteReason::UnknownBackground) {
                _txRouteBgUnknown.fetch_add(1, std::memory_order_relaxed);
            }
            else if (routeReason == RouteReason::BackgroundEvent) {
                _txRouteBgBackground.fetch_add(1, std::memory_order_relaxed);
            }
        }

        const auto loggedTrackIndex = TrackIndexForFrame(out, cfg.prioritySwing);
        const bool shouldLogSubmit =
            loggedTrackIndex != 2 ||
            out.eventType == EventType::Unknown ||
            submitLeadUs > dueWindowUs;
        if (shouldLogSubmit &&
            ShouldEmitWindowedProbe(
                s_trackSubmitProbeWindowUs,
                s_trackSubmitProbeLines,
                nowUs,
                8)) {
            logger::info(
                "[Haptics][ProbeTrack] site=track/submit seq={} evt={} fg={} lead={}us dueWin={}us motor={}/{} conf={:.2f} prio={} track={}",
                out.seq,
                ToString(out.eventType),
                foreground ? 1 : 0,
                submitLeadUs,
                dueWindowUs,
                static_cast<int>(out.leftMotor),
                static_cast<int>(out.rightMotor),
                out.confidence,
                static_cast<int>(out.priority),
                static_cast<int>(loggedTrackIndex));
        }

        return true;
    }

    void HidOutput::FlushStateTrackOnReaderThread(hid_device* device, const HapticsConfig& cfg)
    {
        static std::atomic<std::uint64_t> s_trackSelectProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackSelectProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackNoDueProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackNoDueProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackRepeatProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackRepeatProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackPromoteProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackPromoteProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackReleaseDropProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackReleaseDropProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackFirstRenderProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackFirstRenderProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_trackLivenessProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_trackLivenessProbeLines{ 0 };

        const auto maxSendPerFlush = std::clamp<std::uint32_t>(cfg.hidMaxSendPerFlush, 1u, 8u);
        const auto dueWindowUs = std::max<std::uint64_t>(
            static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs),
            static_cast<std::uint64_t>(cfg.stateTrackLookaheadMinUs));
        auto isRenderedLane = [&](std::uint8_t index) {
            return IsRenderedTrackLane(cfg, index);
        };

        struct TrackCandidate
        {
            bool valid{ false };
            std::uint8_t index{ 0 };
            bool foreground{ false };
            HidFrame frame{};
            std::uint64_t deadlineUs{ 0 };
            std::uint64_t aheadUs{ 0 };
            std::uint64_t overdueUs{ 0 };
            float gain{ 1.0f };
        };

        auto refreshDepthGauge = [&]() {
            std::uint32_t fgDepth = 0;
            std::uint32_t bgDepth = 0;
            std::scoped_lock lock(_trackMutex);
            for (const auto& track : _trackStates) {
                const bool laneValid = track.active.valid || track.pending.valid;
                if (!laneValid) {
                    continue;
                }
                const bool laneForeground = track.active.valid ? track.active.foreground : track.pending.foreground;
                if (laneForeground) {
                    ++fgDepth;
                }
                else {
                    ++bgDepth;
                }
            }
            _txQueueDepthFg.store(fgDepth, std::memory_order_relaxed);
            _txQueueDepthBg.store(bgDepth, std::memory_order_relaxed);
        };

        auto patchLeaseAlive = [&](const TrackSlotState& slot, std::uint64_t tsUs) {
            return slot.valid &&
                slot.eventType == EventType::Footstep &&
                slot.structured.footstep.patchLeaseExpireUs != 0 &&
                tsUs < slot.structured.footstep.patchLeaseExpireUs;
        };

        auto pruneExpiredSlot = [&](TrackSlotState& slot, std::uint64_t nowUs) {
            if (slot.valid &&
                slot.releaseEndUs != 0 &&
                nowUs >= slot.releaseEndUs &&
                !patchLeaseAlive(slot, nowUs)) {
                slot = TrackSlotState{};
                _txTrackDrop.fetch_add(1, std::memory_order_relaxed);
            }
        };

        auto stopReleaseWindowForEvent = [&](EventType type) -> std::uint64_t {
            return std::clamp<std::uint64_t>(
                TrackReleaseWindowUs(type) / 4ull,
                10000ull,
                26000ull);
        };

        auto clearStopIntent = [&](TrackState& track) {
            track.stopPending = false;
            track.stopRequestedUs = 0;
            track.stopSeq = 0;
            track.stopEventType = EventType::Unknown;
        };

        auto applyPendingStop = [&](TrackState& track, std::uint64_t nowUs) {
            if (!track.stopPending) {
                return false;
            }

            bool changed = false;
            if (track.pending.valid) {
                track.pending = TrackSlotState{};
                _txTrackDrop.fetch_add(1, std::memory_order_relaxed);
                changed = true;
            }

            if (track.active.valid) {
                const auto stopEndUs = nowUs + stopReleaseWindowForEvent(track.active.eventType);
                if (track.active.releaseEndUs == 0 || track.active.releaseEndUs > stopEndUs) {
                    track.active.releaseEndUs = stopEndUs;
                    changed = true;
                }
                if (track.active.supersededAtUs == 0) {
                    track.active.supersededAtUs = nowUs;
                }
                if (track.nextReadyUs == 0 || track.nextReadyUs > stopEndUs) {
                    track.nextReadyUs = stopEndUs;
                }
            }
            else if (track.nextReadyUs != 0) {
                track.nextReadyUs = 0;
                changed = true;
            }

            track.livenessDeadlineUs = 0;
            clearStopIntent(track);
            if (changed) {
                _txStopFlushes.fetch_add(1, std::memory_order_relaxed);
            }
            return changed;
        };

        std::uint32_t sentThisFlush = 0;
        while (sentThisFlush < maxSendPerFlush) {
            const auto nowUs = ToQPC(Now());
            const auto supersedeGraceForEvent = [&](EventType type) -> std::uint64_t {
                return std::clamp<std::uint64_t>(
                    TrackReleaseWindowUs(type) / 6ull,
                    8000ull,
                    20000ull);
            };
            auto maybeArmCadenceStop = [&](TrackState& track, std::uint8_t index, std::uint64_t tsUs) {
                if (!UsesCadenceLiveness(cfg, index) ||
                    track.stopPending ||
                    !track.active.valid ||
                    track.pending.valid ||
                    track.livenessDeadlineUs == 0 ||
                    tsUs < track.livenessDeadlineUs) {
                    return false;
                }

                track.stopPending = true;
                track.stopRequestedUs = tsUs;
                track.stopSeq = track.active.seq;
                track.stopEventType = track.active.eventType;
                track.livenessDeadlineUs = 0;

                if (ShouldEmitWindowedProbe(
                        s_trackLivenessProbeWindowUs,
                        s_trackLivenessProbeLines,
                        tsUs,
                        6)) {
                    logger::info(
                        "[Haptics][ProbeTrack] site=track/liveness_stop lane={} seq={} evt={} gap={}us action=queue_stop",
                        static_cast<int>(index),
                        track.active.seq,
                        ToString(track.active.eventType),
                        (track.cadenceLastTokenUs != 0 && tsUs > track.cadenceLastTokenUs) ?
                            (tsUs - track.cadenceLastTokenUs) :
                            0ull);
                }
                return true;
            };
            bool coolingOnlyDeferred = false;
            {
                std::scoped_lock lock(_trackMutex);
                bool hasLiveLane = false;
                bool allCooling = true;
                for (std::uint8_t i = 0; i < _trackStates.size(); ++i) {
                    auto& track = _trackStates[i];
                    pruneExpiredSlot(track.active, nowUs);
                    pruneExpiredSlot(track.pending, nowUs);
                    (void)maybeArmCadenceStop(track, i, nowUs);
                    (void)applyPendingStop(track, nowUs);
                    if (!track.active.valid && !track.pending.valid) {
                        track.nextReadyUs = 0;
                        track.livenessDeadlineUs = 0;
                        track.cadenceLastTokenUs = 0;
                    }

                    const bool laneValid = track.active.valid || track.pending.valid;
                    if (!laneValid) {
                        continue;
                    }
                    hasLiveLane = true;
                    const std::uint64_t laneReadyUs = track.nextReadyUs;
                    if (laneReadyUs == 0 || nowUs >= laneReadyUs) {
                        allCooling = false;
                        break;
                    }
                }
                coolingOnlyDeferred = hasLiveLane && allCooling;
            }
            if (coolingOnlyDeferred) {
                refreshDepthGauge();
                return;
            }

            TrackCandidate selected{};
            bool hasAnyTrack = false;
            bool cooldownBlocked = false;
            std::uint64_t nearestFutureUs = 0;
            {
                std::scoped_lock lock(_trackMutex);
                auto maybePromotePending = [&](std::uint8_t index, const char* reason) {
                    auto& track = _trackStates[index];
                    if (!track.pending.valid) {
                        return false;
                    }
                    if (track.nextReadyUs != 0 && nowUs < track.nextReadyUs) {
                        return false;
                    }

                    track.active = track.pending;
                    track.pending = TrackSlotState{};
                    clearStopIntent(track);

                    const bool shouldLogPromote =
                        index != 2 || std::strcmp(reason, "supersede") == 0;
                    if (shouldLogPromote &&
                        ShouldEmitWindowedProbe(
                            s_trackPromoteProbeWindowUs,
                            s_trackPromoteProbeLines,
                            nowUs,
                            6)) {
                        logger::info(
                            "[Haptics][ProbeTrack] site=track/promote lane={} evt={} seq={} reason={}",
                            static_cast<int>(index),
                            ToString(track.active.eventType),
                            track.active.seq,
                            reason);
                    }
                    return true;
                };

                auto maybeRefreshTokenLane = [&](std::uint8_t index) {
                    auto& track = _trackStates[index];
                    if (!IsTokenTrackLane(cfg, index) || !track.active.valid || !track.pending.valid) {
                        return false;
                    }
                    if (track.nextReadyUs != 0 && nowUs < track.nextReadyUs) {
                        return false;
                    }

                    auto& active = track.active;
                    const auto pending = track.pending;
                    active.eventType = pending.eventType;
                    active.sourceType = pending.sourceType;
                    active.priority = std::max(active.priority, pending.priority);
                    active.confidence = std::max(active.confidence, pending.confidence);
                    active.hint = active.hint || pending.hint;
                    active.baseLeft = std::max(active.baseLeft, pending.baseLeft);
                    active.baseRight = std::max(active.baseRight, pending.baseRight);
                    active.sourceQpc = active.renderedOnce ?
                        pending.sourceQpc :
                        ((active.sourceQpc != 0 && pending.sourceQpc != 0) ?
                                std::min(active.sourceQpc, pending.sourceQpc) :
                                std::max(active.sourceQpc, pending.sourceQpc));
                    active.deadlineUs = std::max(
                        track.nextReadyUs,
                        (pending.deadlineUs != 0) ? pending.deadlineUs : pending.lastUpdateUs);
                    active.lastUpdateUs = nowUs;
                    active.releaseEndUs = std::max(
                        active.releaseEndUs,
                        std::max(active.deadlineUs, track.nextReadyUs) + TrackReleaseWindowUs(active.eventType));
                    active.structured.footstep.gait = (pending.structured.footstep.gait != FootstepTruthGait::Unknown) ?
                        pending.structured.footstep.gait :
                        active.structured.footstep.gait;
                    active.structured.footstep.side = (pending.structured.footstep.side != FootstepTruthSide::Unknown) ?
                        pending.structured.footstep.side :
                        active.structured.footstep.side;
                    active.structured.footstep.texturePreset = (pending.structured.footstep.texturePreset != FootstepTexturePreset::None) ?
                        pending.structured.footstep.texturePreset :
                        active.structured.footstep.texturePreset;
                    active.structured.footstep.modifier.ampScale = std::max(
                        active.structured.footstep.modifier.ampScale,
                        pending.structured.footstep.modifier.ampScale);
                    active.structured.footstep.modifier.panSigned = pending.structured.footstep.modifier.panSigned;
                    active.structured.footstep.modifier.scorePermille = std::max(
                        active.structured.footstep.modifier.scorePermille,
                        pending.structured.footstep.modifier.scorePermille);
                    active.structured.footstep.modifier.targetEndUs = std::max(
                        active.structured.footstep.modifier.targetEndUs,
                        pending.structured.footstep.modifier.targetEndUs);
                    active.structured.footstep.modifier.provisional =
                        active.structured.footstep.modifier.provisional || pending.structured.footstep.modifier.provisional;
                    active.seq = pending.seq;
                    active.epoch = pending.epoch;
                    active.supersededAtUs = 0;
                    track.pending = TrackSlotState{};
                    clearStopIntent(track);

                    if (ShouldEmitWindowedProbe(
                            s_trackPromoteProbeWindowUs,
                            s_trackPromoteProbeLines,
                            nowUs,
                            6)) {
                        logger::info(
                            "[Haptics][ProbeTrack] site=track/promote lane={} evt={} seq={} reason=token_refresh",
                            static_cast<int>(index),
                            ToString(active.eventType),
                            active.seq);
                    }
                    return true;
                };

                auto maybeApplyFootstepLivePatch = [&](std::uint8_t index) {
                    if (index != 2 || !cfg.enableFootstepAudioModifierLivePatch) {
                        return false;
                    }

                    auto& track = _trackStates[index];
                    auto& active = track.active;
                    const bool silentSeedCandidate =
                        active.valid &&
                        active.eventType == EventType::Footstep &&
                        !active.renderedOnce &&
                        active.baseLeft == 0 &&
                        active.baseRight == 0 &&
                        active.sourceQpc != 0;
                    if (!active.valid ||
                        active.eventType != EventType::Footstep ||
                        active.sourceQpc == 0) {
                        return false;
                    }
                    if (!active.renderedOnce && !silentSeedCandidate) {
                        return false;
                    }

                    std::uint64_t patchTruthUs = 0;
                    std::uint64_t patchTargetEndUs = 0;
                    float patchAmpScale = 1.0f;
                    float patchPanSigned = 0.0f;
                    std::uint32_t patchLeaseUs = 0;
                    std::uint16_t patchScorePermille = 0;
                    const char* patchSource = nullptr;

                    std::uint64_t sessionPatchRevision = 0;
                    if (cfg.enableFootstepTruthSessionLivePatch) {
                        auto sessionPatch = FootstepTruthSessionShadow::GetSingleton().TryGetLivePatchForTruth(
                            active.sourceQpc,
                            active.structured.footstep.modifier.revisionApplied,
                            nowUs);
                        if (sessionPatch.has_value()) {
                            patchTruthUs = sessionPatch->truthUs;
                            patchTargetEndUs = sessionPatch->targetEndUs;
                            patchAmpScale = sessionPatch->ampScale;
                            patchPanSigned = sessionPatch->panSigned;
                            patchLeaseUs = sessionPatch->patchLeaseUs;
                            patchScorePermille = sessionPatch->scorePermille;
                            sessionPatchRevision = sessionPatch->revision;
                            patchSource = sessionPatch->provisional ? "session_provisional" : "session_patch";
                        }
                    }

                    if (patchSource == nullptr) {
                        auto patch = FootstepAudioMatcher::GetSingleton().TryConsumeLivePatchForTruth(
                            active.sourceQpc,
                            nowUs);
                        if (!patch.has_value()) {
                            return false;
                        }
                        patchTruthUs = patch->truthUs;
                        patchTargetEndUs = patch->targetEndUs;
                        patchAmpScale = patch->ampScale;
                        patchPanSigned = patch->panSigned;
                        patchLeaseUs = patch->patchLeaseUs;
                        patchScorePermille = patch->scorePermille;
                        patchSource = patch->fromRecentMemory ? "recent_memory" : "audio_patch";
                    }

                    const auto [prevLeft, prevRight] = ResolveFootstepStrideMotors(
                        cfg,
                        active.structured.footstep.seedLeft,
                        active.structured.footstep.seedRight,
                        active.structured.footstep.modifier.ampScale,
                        active.structured.footstep.modifier.panSigned);
                    const bool silentSeed = !active.renderedOnce && prevLeft == 0 && prevRight == 0;
                    active.structured.footstep.modifier.ampScale = patchAmpScale;
                    active.structured.footstep.modifier.panSigned = patchPanSigned;
                    active.structured.footstep.modifier.scorePermille = std::max(
                        active.structured.footstep.modifier.scorePermille,
                        patchScorePermille);
                    active.structured.footstep.modifier.targetEndUs = std::max(active.structured.footstep.modifier.targetEndUs, patchTargetEndUs);
                    active.structured.footstep.modifier.provisional = std::strcmp(patchSource, "session_patch") != 0 &&
                        std::strcmp(patchSource, "audio_patch") != 0;
                    const auto [nextLeft, nextRight] = ResolveFootstepStrideMotors(
                        cfg,
                        active.structured.footstep.seedLeft,
                        active.structured.footstep.seedRight,
                        active.structured.footstep.modifier.ampScale,
                        active.structured.footstep.modifier.panSigned);
                    if (silentSeed) {
                        active.deadlineUs = nowUs;
                    }

                    active.structured.footstep.patchLeaseExpireUs = std::max(
                        active.structured.footstep.patchLeaseExpireUs,
                        active.sourceQpc + static_cast<std::uint64_t>(patchLeaseUs));
                    active.releaseEndUs = std::max(
                        active.releaseEndUs,
                        std::max(patchTargetEndUs, active.structured.footstep.modifier.targetEndUs));
                    active.lastUpdateUs = nowUs;
                    if (track.nextReadyUs > nowUs) {
                        track.nextReadyUs = nowUs;
                    }
                    if (sessionPatchRevision != 0) {
                        active.structured.footstep.modifier.revisionApplied = sessionPatchRevision;
                        FootstepTruthSessionShadow::GetSingleton().NoteLivePatchApplied(
                            patchTruthUs,
                            sessionPatchRevision);
                    }

                    if (ShouldEmitWindowedProbe(
                            s_trackPromoteProbeWindowUs,
                            s_trackPromoteProbeLines,
                            nowUs,
                            6)) {
                        logger::info(
                            "[Haptics][ProbeTrack] site=track/live_patch lane={} seq={} truth={} source={} score={:.2f} amp={:.2f} pan={:.2f} texture={} targetEndDelta={}us lease={}us motor={}/{}->{}/{}",
                            static_cast<int>(index),
                            active.seq,
                            patchTruthUs,
                            patchSource,
                            static_cast<float>(patchScorePermille) / 1000.0f,
                            patchAmpScale,
                            patchPanSigned,
                            ToString(active.structured.footstep.texturePreset),
                            (patchTargetEndUs > patchTruthUs) ? (patchTargetEndUs - patchTruthUs) : 0ull,
                            patchLeaseUs,
                            static_cast<int>(prevLeft),
                            static_cast<int>(prevRight),
                            static_cast<int>(nextLeft),
                            static_cast<int>(nextRight));
                    }
                    return true;
                };

                for (std::uint8_t i = 0; i < _trackStates.size(); ++i) {
                    auto& track = _trackStates[i];
                    const bool renderedLane = isRenderedLane(i);
                    const bool tokenLane = IsTokenTrackLane(cfg, i);
                    pruneExpiredSlot(track.active, nowUs);
                    pruneExpiredSlot(track.pending, nowUs);
                    (void)maybeArmCadenceStop(track, i, nowUs);
                    (void)applyPendingStop(track, nowUs);
                    if (!track.active.valid && !track.pending.valid) {
                        track.nextReadyUs = 0;
                        track.livenessDeadlineUs = 0;
                        track.cadenceLastTokenUs = 0;
                    }

                    if (!track.active.valid) {
                        (void)maybePromotePending(i, tokenLane ? "token_start" : "revive");
                    }
                    else if (tokenLane &&
                             track.pending.valid &&
                             (track.nextReadyUs == 0 || nowUs >= track.nextReadyUs)) {
                        (void)maybeRefreshTokenLane(i);
                    }
                    else if (tokenLane && track.pending.valid) {
                        cooldownBlocked = true;
                        if (nearestFutureUs == 0 || track.nextReadyUs < nearestFutureUs) {
                            nearestFutureUs = track.nextReadyUs;
                        }
                    }
                    else if (renderedLane &&
                             track.pending.valid &&
                             track.pending.epoch > track.active.epoch &&
                             track.active.supersededAtUs != 0) {
                        const auto supersedeReadyUs = std::max(
                            track.nextReadyUs,
                            track.active.supersededAtUs + supersedeGraceForEvent(track.active.eventType));
                        if (track.active.renderedOnce && nowUs >= supersedeReadyUs) {
                            (void)maybePromotePending(i, "supersede");
                        }
                        else if (nearestFutureUs == 0 || supersedeReadyUs < nearestFutureUs) {
                            nearestFutureUs = supersedeReadyUs;
                        }
                    }
                    else if (!renderedLane &&
                             track.pending.valid &&
                             (track.nextReadyUs == 0 || nowUs >= track.nextReadyUs)) {
                        (void)maybePromotePending(i, "ready_switch");
                    }

                    if (renderedLane) {
                        (void)maybeApplyFootstepLivePatch(i);
                    }

                    const bool laneValid = track.active.valid || track.pending.valid;
                    if (!laneValid) {
                        continue;
                    }
                    hasAnyTrack = true;

                    if (!track.active.valid) {
                        const auto idleReadyUs = track.nextReadyUs;
                        if (idleReadyUs != 0 && nowUs < idleReadyUs) {
                            cooldownBlocked = true;
                            if (nearestFutureUs == 0 || idleReadyUs < nearestFutureUs) {
                                nearestFutureUs = idleReadyUs;
                            }
                        }
                        const auto pendingDeadlineUs = (track.pending.deadlineUs != 0) ?
                            track.pending.deadlineUs :
                            track.pending.lastUpdateUs;
                        if (pendingDeadlineUs != 0 &&
                            (nearestFutureUs == 0 || pendingDeadlineUs < nearestFutureUs)) {
                            nearestFutureUs = pendingDeadlineUs;
                        }
                        continue;
                    }

                    auto& active = track.active;
                    const auto rawDeadlineUs = (active.deadlineUs != 0) ? active.deadlineUs : active.lastUpdateUs;
                    if (rawDeadlineUs == 0) {
                        continue;
                    }

                    std::uint64_t deadlineUs = rawDeadlineUs;
                    if (renderedLane) {
                        const auto renderedReadyUs = track.nextReadyUs;
                        if (renderedReadyUs != 0 && nowUs < renderedReadyUs) {
                            cooldownBlocked = true;
                            if (nearestFutureUs == 0 || renderedReadyUs < nearestFutureUs) {
                                nearestFutureUs = renderedReadyUs;
                            }
                            continue;
                        }
                        if (renderedReadyUs == 0 &&
                            rawDeadlineUs > nowUs &&
                            rawDeadlineUs > (nowUs + dueWindowUs)) {
                            if (nearestFutureUs == 0 || rawDeadlineUs < nearestFutureUs) {
                                nearestFutureUs = rawDeadlineUs;
                            }
                            continue;
                        }
                        if (renderedReadyUs != 0) {
                            deadlineUs = renderedReadyUs;
                        }
                    }
                    else {
                        if (track.nextReadyUs != 0 && nowUs < track.nextReadyUs) {
                            cooldownBlocked = true;
                            if (nearestFutureUs == 0 || track.nextReadyUs < nearestFutureUs) {
                                nearestFutureUs = track.nextReadyUs;
                            }
                            continue;
                        }

                        const bool inDueWindow = deadlineUs <= (nowUs + dueWindowUs) || nowUs >= deadlineUs;
                        if (!inDueWindow) {
                            if (nearestFutureUs == 0 || deadlineUs < nearestFutureUs) {
                                nearestFutureUs = deadlineUs;
                            }
                            continue;
                        }
                    }

                    float gain = 1.0f;
                    std::uint64_t overdueUs = 0;
                    std::uint64_t effectiveReleaseEndUs = active.releaseEndUs;
                    std::uint8_t renderBaseLeft = active.baseLeft;
                    std::uint8_t renderBaseRight = active.baseRight;
                    if (active.eventType == EventType::Footstep) {
                        if (cfg.enableStateTrackFootstepTextureRenderer) {
                            const auto textured = RenderFootstepTexture(
                                cfg,
                                active.sourceQpc,
                                active.releaseEndUs,
                                active.structured.footstep.modifier.targetEndUs,
                                active.structured.footstep.seedLeft,
                                active.structured.footstep.seedRight,
                                active.structured.footstep.gait,
                                active.structured.footstep.side,
                                active.structured.footstep.texturePreset,
                                active.structured.footstep.modifier.ampScale,
                                active.structured.footstep.modifier.panSigned,
                                active.structured.footstep.modifier.scorePermille,
                                active.structured.footstep.modifier.provisional,
                                nowUs);
                            renderBaseLeft = textured.left;
                            renderBaseRight = textured.right;
                            effectiveReleaseEndUs = std::max(effectiveReleaseEndUs, textured.effectiveEndUs);
                        }
                        else {
                            const auto motors = ResolveFootstepStrideMotors(
                                cfg,
                                active.structured.footstep.seedLeft,
                                active.structured.footstep.seedRight,
                                active.structured.footstep.modifier.ampScale,
                                active.structured.footstep.modifier.panSigned);
                            renderBaseLeft = motors.first;
                            renderBaseRight = motors.second;
                            effectiveReleaseEndUs = std::max(
                                effectiveReleaseEndUs,
                                active.structured.footstep.modifier.targetEndUs);
                        }
                    }
                    if (nowUs > deadlineUs) {
                        overdueUs = nowUs - deadlineUs;
                        const auto releaseSpanUs = (effectiveReleaseEndUs > deadlineUs) ?
                            (effectiveReleaseEndUs - deadlineUs) :
                            1ull;
                        const float minGain = (active.eventType == EventType::Footstep) ? 0.0f : 0.12f;
                        gain = std::clamp(
                            1.0f - (static_cast<float>(overdueUs) / static_cast<float>(releaseSpanUs)),
                            minGain,
                            1.0f);
                    }
                    const auto left = static_cast<std::uint8_t>(std::clamp(
                        static_cast<float>(renderBaseLeft) * gain, 0.0f, 255.0f));
                    const auto right = static_cast<std::uint8_t>(std::clamp(
                        static_cast<float>(renderBaseRight) * gain, 0.0f, 255.0f));
                    if (left == 0 && right == 0) {
                        if (patchLeaseAlive(active, nowUs)) {
                            if (track.nextReadyUs == 0 || track.nextReadyUs > (nowUs + 4000ull)) {
                                track.nextReadyUs = nowUs + 4000ull;
                            }
                            continue;
                        }
                        if (overdueUs > static_cast<std::uint64_t>(cfg.stateTrackRepeatKeepMaxOverdueUs)) {
                            active = TrackSlotState{};
                            _txTrackDrop.fetch_add(1, std::memory_order_relaxed);
                        }
                        continue;
                    }

                    const bool choose = !selected.valid ||
                        deadlineUs < selected.deadlineUs ||
                        (deadlineUs == selected.deadlineUs && active.foreground && !selected.foreground) ||
                        (deadlineUs == selected.deadlineUs &&
                            active.foreground == selected.foreground &&
                            active.priority > selected.frame.priority);
                    if (!choose) {
                        continue;
                    }

                    selected.valid = true;
                    selected.index = i;
                    selected.foreground = active.foreground;
                    selected.deadlineUs = deadlineUs;
                    selected.aheadUs = (deadlineUs > nowUs) ? (deadlineUs - nowUs) : 0ull;
                    selected.overdueUs = overdueUs;
                    selected.gain = gain;
                    selected.frame.qpc = active.sourceQpc;
                    selected.frame.qpcTarget = deadlineUs;
                    selected.frame.seq = active.seq;
                    selected.frame.eventType = active.eventType;
                    selected.frame.sourceType = active.sourceType;
                    selected.frame.priority = active.priority;
                    selected.frame.confidence = active.confidence;
                    selected.frame.foregroundHint = active.hint;
                    selected.frame.leftMotor = left;
                    selected.frame.rightMotor = right;
                }
            }

            if (!selected.valid) {
                if (hasAnyTrack) {
                    _txFlushNoSelectPending.fetch_add(1, std::memory_order_relaxed);
                    if (!cooldownBlocked) {
                        _txFlushLookaheadMiss.fetch_add(1, std::memory_order_relaxed);
                    }
                    _txStateFutureSkip.fetch_add(1, std::memory_order_relaxed);
                    if (ShouldEmitWindowedProbe(
                            s_trackNoDueProbeWindowUs,
                            s_trackNoDueProbeLines,
                            nowUs,
                            8)) {
                        logger::info(
                            "[Haptics][ProbeTrack] site=track/no_due dueWin={}us nearestFutureGap={}us depthFg={} depthBg={} cooldownBlock={}",
                            dueWindowUs,
                            (nearestFutureUs > nowUs + dueWindowUs) ? (nearestFutureUs - (nowUs + dueWindowUs)) : 0ull,
                            _txQueueDepthFg.load(std::memory_order_relaxed),
                            _txQueueDepthBg.load(std::memory_order_relaxed),
                            cooldownBlocked ? 1 : 0);
                    }
                }

                if (_lastSentValid &&
                    (_lastSentLeft != 0 || _lastSentRight != 0) &&
                    (_lastSendQpc == 0 || nowUs >= (_lastSendQpc + static_cast<std::uint64_t>(cfg.hidIdleRepeatIntervalUs)))) {
                    std::scoped_lock lock(_deviceMutex);
                    _device = device;
                    if (SendVibrateCommandUnlocked(0, 0)) {
                        _sendWriteOk.fetch_add(1, std::memory_order_relaxed);
                        _totalFramesSent.fetch_add(1, std::memory_order_relaxed);
                        _totalBytesSent.fetch_add(48, std::memory_order_relaxed);
                        _lastSentLeft = 0;
                        _lastSentRight = 0;
                        _lastSentEvent = EventType::Unknown;
                        _lastSentValid = true;
                        _lastSendQpc = nowUs;
                        _lastTargetQpc = nowUs;
                    }
                }
                break;
            }

            const bool shouldLogSelect =
                selected.index != 2 ||
                selected.frame.eventType == EventType::Unknown ||
                selected.aheadUs > (dueWindowUs + 600ull) ||
                selected.overdueUs >= 1400ull ||
                selected.gain < 0.95f;
            if (shouldLogSelect &&
                ShouldEmitWindowedProbe(
                    s_trackSelectProbeWindowUs,
                    s_trackSelectProbeLines,
                    nowUs,
                    10)) {
                logger::info(
                    "[Haptics][ProbeTrack] site=track/select lane={} seq={} evt={} fg={} motor={}/{} deadline={} ahead={}us over={}us gain={:.2f}",
                    static_cast<int>(selected.index),
                    selected.frame.seq,
                    ToString(selected.frame.eventType),
                    selected.foreground ? 1 : 0,
                    static_cast<int>(selected.frame.leftMotor),
                    static_cast<int>(selected.frame.rightMotor),
                    selected.deadlineUs,
                    selected.aheadUs,
                    selected.overdueUs,
                    selected.gain);
            }

            const auto selectedMinRepeatUs = (selected.frame.leftMotor == 0 && selected.frame.rightMotor == 0) ?
                static_cast<std::uint64_t>(cfg.hidIdleRepeatIntervalUs) :
                static_cast<std::uint64_t>(cfg.hidMinRepeatIntervalUs);

            if (_lastSentValid) {
                const bool sameOutput =
                    (_lastSentLeft == selected.frame.leftMotor) &&
                    (_lastSentRight == selected.frame.rightMotor) &&
                    (_lastSentEvent == selected.frame.eventType);
                const auto deltaUs = (nowUs > _lastSendQpc) ? (nowUs - _lastSendQpc) : 0ull;
                if (sameOutput && deltaUs < selectedMinRepeatUs) {
                    _txSkippedRepeat.fetch_add(1, std::memory_order_relaxed);
                    const bool dropTrack = selected.overdueUs >
                        static_cast<std::uint64_t>(cfg.stateTrackRepeatKeepMaxOverdueUs);
                    {
                        std::scoped_lock lock(_trackMutex);
                        auto& track = _trackStates[selected.index];
                        track.nextReadyUs = std::max(track.nextReadyUs, nowUs + selectedMinRepeatUs);
                        if (dropTrack && track.active.valid) {
                            track.active = TrackSlotState{};
                            _txTrackDrop.fetch_add(1, std::memory_order_relaxed);
                            _txTrackRepeatDrop.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    if (ShouldEmitWindowedProbe(
                            s_trackRepeatProbeWindowUs,
                            s_trackRepeatProbeLines,
                            nowUs,
                            12)) {
                        logger::info(
                            "[Haptics][ProbeTrack] site=track/repeat_guard seq={} evt={} delta={}us minReq={}us over={}us action={}",
                            selected.frame.seq,
                            ToString(selected.frame.eventType),
                            deltaUs,
                            selectedMinRepeatUs,
                            selected.overdueUs,
                            dropTrack ? "drop_track" : "keep_track");
                    }
                    refreshDepthGauge();
                    break;
                }
            }

            bool ok = false;
            {
                std::scoped_lock lock(_deviceMutex);
                _device = device;
                ok = SendVibrateCommandUnlocked(selected.frame.leftMotor, selected.frame.rightMotor);
            }
            if (!ok) {
                _sendFailures.fetch_add(1, std::memory_order_relaxed);
                _txSendFail.fetch_add(1, std::memory_order_relaxed);
                break;
            }

            _sendWriteOk.fetch_add(1, std::memory_order_relaxed);
            _totalFramesSent.fetch_add(1, std::memory_order_relaxed);
            _totalBytesSent.fetch_add(48, std::memory_order_relaxed);
            if (selected.foreground) {
                _txDequeuedFg.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                _txDequeuedBg.fetch_add(1, std::memory_order_relaxed);
            }
            _txTrackSelect.fetch_add(1, std::memory_order_relaxed);
            const auto sendUs = ToQPC(Now());
            const bool renderedSelectedLane = isRenderedLane(selected.index);
            std::optional<std::uint32_t> firstRenderLatencyUs{};

            {
                std::scoped_lock lock(_trackMutex);
                auto& track = _trackStates[selected.index];
                track.nextReadyUs = sendUs + selectedMinRepeatUs;
                auto& active = track.active;
                if (active.valid) {
                    if (!active.renderedOnce && active.sourceQpc != 0 && sendUs > active.sourceQpc) {
                        firstRenderLatencyUs = static_cast<std::uint32_t>(sendUs - active.sourceQpc);
                    }
                    active.renderedOnce = true;
                    if (active.releaseEndUs != 0 &&
                        sendUs >= active.releaseEndUs &&
                        !patchLeaseAlive(active, sendUs)) {
                        if (ShouldEmitWindowedProbe(
                                s_trackReleaseDropProbeWindowUs,
                                s_trackReleaseDropProbeLines,
                                sendUs,
                                8)) {
                            logger::info(
                                "[Haptics][ProbeTrack] site=track/release_drop lane={} seq={} evt={} rendered={} reason=release_complete sourceAge={}us pendingValid={}",
                                static_cast<int>(selected.index),
                                active.seq,
                                ToString(active.eventType),
                                active.renderedOnce ? 1 : 0,
                                (active.sourceQpc != 0 && sendUs > active.sourceQpc) ? (sendUs - active.sourceQpc) : 0ull,
                                track.pending.valid ? 1 : 0);
                        }
                        active = TrackSlotState{};
                        _txTrackDrop.fetch_add(1, std::memory_order_relaxed);
                    }
                    else if (renderedSelectedLane) {
                        active.deadlineUs = track.nextReadyUs;
                        active.lastUpdateUs = sendUs;
                    }
                    else {
                        active.baseLeft = static_cast<std::uint8_t>(std::clamp(
                            static_cast<int>(active.baseLeft) - 1, 0, 255));
                        active.baseRight = static_cast<std::uint8_t>(std::clamp(
                            static_cast<int>(active.baseRight) - 1, 0, 255));
                        if (active.baseLeft == 0 && active.baseRight == 0) {
                            active = TrackSlotState{};
                            _txTrackDrop.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
            }

            if (firstRenderLatencyUs.has_value() &&
                firstRenderLatencyUs.value() >= 12000u &&
                ShouldEmitWindowedProbe(
                    s_trackFirstRenderProbeWindowUs,
                    s_trackFirstRenderProbeLines,
                    sendUs,
                    8)) {
                logger::info(
                    "[Haptics][ProbeTrack] site=track/first_render_slow lane={} seq={} evt={} first={}us over={}us repeat={}us gain={:.2f}",
                    static_cast<int>(selected.index),
                    selected.frame.seq,
                    ToString(selected.frame.eventType),
                    firstRenderLatencyUs.value(),
                    selected.overdueUs,
                    selectedMinRepeatUs,
                    selected.gain);
            }

            if (selected.index == 2 &&
                selected.frame.eventType == EventType::Footstep &&
                firstRenderLatencyUs.has_value()) {
                FootstepTruthProbe::GetSingleton().ObserveShadowRender(
                    sendUs,
                    selected.frame.seq,
                    selected.frame.eventType,
                    selected.frame.leftMotor,
                    selected.frame.rightMotor);
            }

            const auto latencyUs = (selected.frame.qpc != 0 && sendUs > selected.frame.qpc) ?
                static_cast<std::uint32_t>(sendUs - selected.frame.qpc) :
                0u;
            const auto expectedDeltaUs = (_lastTargetQpc != 0 && selected.frame.qpcTarget >= _lastTargetQpc) ?
                static_cast<std::uint32_t>(selected.frame.qpcTarget - _lastTargetQpc) :
                0u;
            const auto actualDeltaUs = (_lastSendQpc != 0 && sendUs >= _lastSendQpc) ?
                static_cast<std::uint32_t>(sendUs - _lastSendQpc) :
                0u;
            const auto jitterUs = (expectedDeltaUs > actualDeltaUs) ?
                (expectedDeltaUs - actualDeltaUs) :
                (actualDeltaUs - expectedDeltaUs);
            PushLatencySample(latencyUs, jitterUs);
            PushTrackTimingSample(
                static_cast<std::uint32_t>(std::min<std::uint64_t>(selected.overdueUs, std::numeric_limits<std::uint32_t>::max())),
                firstRenderLatencyUs);
            _lastSendQpc = sendUs;
            _lastTargetQpc = selected.frame.qpcTarget;
            _lastSentLeft = selected.frame.leftMotor;
            _lastSentRight = selected.frame.rightMotor;
            _lastSentEvent = selected.frame.eventType;
            _lastSentValid = true;
            ++sentThisFlush;
            refreshDepthGauge();
            if (renderedSelectedLane) {
                break;
            }
        }

        if (sentThisFlush >= maxSendPerFlush) {
            bool hasPending = false;
            {
                std::scoped_lock lock(_trackMutex);
                for (const auto& track : _trackStates) {
                    if (!track.active.valid && !track.pending.valid) {
                        continue;
                    }
                    hasPending = true;
                    break;
                }
            }
            if (hasPending) {
                _txFlushCapHit.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    bool HidOutput::PopDueFront(
        std::deque<HidFrame>& queue,
        std::uint64_t nowUs,
        std::uint64_t lookaheadUs,
        HidFrame& outFrame)
    {
        if (queue.empty()) {
            return false;
        }

        const auto dueUs = queue.front().qpcTarget != 0 ? queue.front().qpcTarget : queue.front().qpc;
        if (dueUs > (nowUs + lookaheadUs)) {
            return false;
        }

        outFrame = queue.front();
        queue.pop_front();
        return true;
    }

    bool HidOutput::TrySelectFrame(
        std::uint64_t nowUs,
        SelectedFrame& outSelected,
        std::uint64_t lookaheadUs,
        bool fgPreempt,
        std::uint32_t bgBudget)
    {
        HidFrame candidate{};
        bool selected = false;
        bool isFg = false;
        bool hadFgPending = false;
        bool forceFgBudget = false;
        std::uint32_t fgDepthAfter = 0;
        std::uint32_t bgDepthAfter = 0;

        {
            std::scoped_lock lock(_queueMutex);
            hadFgPending = !_fgQueue.empty();

            if (fgPreempt) {
                bool forceFg = false;
                if (bgBudget > 0 && _bgDequeuedSinceFg >= bgBudget && !_fgQueue.empty()) {
                    forceFg = true;
                }

                if (forceFg && PopDueFront(_fgQueue, nowUs, lookaheadUs + 1000u, candidate)) {
                    selected = true;
                    isFg = true;
                    forceFgBudget = true;
                }
                else if (PopDueFront(_fgQueue, nowUs, lookaheadUs, candidate)) {
                    selected = true;
                    isFg = true;
                }
                else if (PopDueFront(_bgQueue, nowUs, lookaheadUs, candidate)) {
                    selected = true;
                    isFg = false;
                }
            }
            else {
                if (PopDueFront(_bgQueue, nowUs, lookaheadUs, candidate)) {
                    selected = true;
                    isFg = false;
                }
                else if (PopDueFront(_fgQueue, nowUs, lookaheadUs, candidate)) {
                    selected = true;
                    isFg = true;
                }
            }

            fgDepthAfter = static_cast<std::uint32_t>(_fgQueue.size());
            bgDepthAfter = static_cast<std::uint32_t>(_bgQueue.size());
            _txQueueDepthFg.store(fgDepthAfter, std::memory_order_relaxed);
            _txQueueDepthBg.store(bgDepthAfter, std::memory_order_relaxed);

            if (!selected) {
                return false;
            }

            if (isFg) {
                _txDequeuedFg.fetch_add(1, std::memory_order_relaxed);
                _bgDequeuedSinceFg = 0;
            }
            else {
                _txDequeuedBg.fetch_add(1, std::memory_order_relaxed);
                if (_bgDequeuedSinceFg < 0xFFFFu) {
                    _bgDequeuedSinceFg += 1u;
                }
            }

            outSelected.frame = candidate;
            outSelected.foreground = isFg;
        }

        if (isFg && forceFgBudget) {
            _txSelectForcedFgBudget.fetch_add(1, std::memory_order_relaxed);
        }
        if (!isFg && hadFgPending) {
            _txSelectBgWhileFgPending.fetch_add(1, std::memory_order_relaxed);

            static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
            static std::atomic<std::uint32_t> s_probeLines{ 0 };
            auto shouldEmitProbe = [&](std::uint64_t tsUs) {
                auto win = s_probeWindowUs.load(std::memory_order_relaxed);
                if (win == 0 || tsUs < win || (tsUs - win) >= 1000000ull) {
                    s_probeWindowUs.store(tsUs, std::memory_order_relaxed);
                    s_probeLines.store(0, std::memory_order_relaxed);
                }
                return s_probeLines.fetch_add(1, std::memory_order_relaxed) < 6;
            };

            if ((candidate.leftMotor != 0 || candidate.rightMotor != 0) && shouldEmitProbe(nowUs)) {
                logger::info(
                    "[Haptics][ProbeTxSelect] bgWhileFgPending evt={} prio={} conf={:.2f} motor={}/{} fgDepth={} bgDepth={} lookahead={}us bgBudget={} fgPreempt={}",
                    ToString(candidate.eventType),
                    static_cast<int>(candidate.priority),
                    candidate.confidence,
                    static_cast<int>(candidate.leftMotor),
                    static_cast<int>(candidate.rightMotor),
                    fgDepthAfter,
                    bgDepthAfter,
                    lookaheadUs,
                    bgBudget,
                    fgPreempt ? 1 : 0);
            }
        }

        return true;
    }

    void HidOutput::AccumulateMerge(MergeFrameState& state, const HidFrame& frame, bool foreground)
    {
        if (!state.initialized) {
            state.initialized = true;
            state.sumLeft = static_cast<float>(frame.leftMotor);
            state.sumRight = static_cast<float>(frame.rightMotor);
            state.count = 1;
            state.maxLeft = frame.leftMotor;
            state.maxRight = frame.rightMotor;
            state.maxConfidence = frame.confidence;
            state.maxPriority = frame.priority;
            state.dominantEvent = frame.eventType;
            state.dominantSource = frame.sourceType;
            state.latestQpc = frame.qpc;
            state.latestTargetQpc = frame.qpcTarget;
            state.foregroundHint = frame.foregroundHint;
            return;
        }

        state.sumLeft += static_cast<float>(frame.leftMotor);
        state.sumRight += static_cast<float>(frame.rightMotor);
        state.count += 1;
        state.maxLeft = std::max(state.maxLeft, frame.leftMotor);
        state.maxRight = std::max(state.maxRight, frame.rightMotor);
        state.maxConfidence = std::max(state.maxConfidence, frame.confidence);
        state.maxPriority = std::max(state.maxPriority, frame.priority);
        if (frame.confidence >= state.maxConfidence) {
            state.dominantEvent = frame.eventType;
            state.dominantSource = frame.sourceType;
        }
        state.latestQpc = std::max(state.latestQpc, frame.qpc);
        state.latestTargetQpc = std::max(state.latestTargetQpc, frame.qpcTarget);
        state.foregroundHint = state.foregroundHint || frame.foregroundHint;

        if (foreground) {
            _txMergedFg.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            _txMergedBg.fetch_add(1, std::memory_order_relaxed);
        }
    }

    HidFrame HidOutput::MergeFrames(const SelectedFrame& selected, std::uint64_t nowUs, std::uint64_t mergeWindowUs)
    {
        if (mergeWindowUs == 0) {
            return selected.frame;
        }

        MergeFrameState state{};
        AccumulateMerge(state, selected.frame, selected.foreground);
        const auto anchorTarget = selected.frame.qpcTarget != 0 ? selected.frame.qpcTarget : selected.frame.qpc;

        std::scoped_lock lock(_queueMutex);
        auto& q = selected.foreground ? _fgQueue : _bgQueue;
        while (!q.empty()) {
            const auto dueUs = q.front().qpcTarget != 0 ? q.front().qpcTarget : q.front().qpc;
            if (dueUs > nowUs + mergeWindowUs) {
                break;
            }

            if (anchorTarget <= dueUs) {
                if (dueUs - anchorTarget > mergeWindowUs) {
                    break;
                }
            }
            else if (anchorTarget - dueUs > mergeWindowUs) {
                break;
            }

            const HidFrame f = q.front();
            q.pop_front();

            if (selected.foreground) {
                _txDequeuedFg.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                _txDequeuedBg.fetch_add(1, std::memory_order_relaxed);
            }
            AccumulateMerge(state, f, selected.foreground);
        }

        _txQueueDepthFg.store(static_cast<std::uint32_t>(_fgQueue.size()), std::memory_order_relaxed);
        _txQueueDepthBg.store(static_cast<std::uint32_t>(_bgQueue.size()), std::memory_order_relaxed);

        HidFrame out = selected.frame;
        if (selected.foreground) {
            out.leftMotor = state.maxLeft;
            out.rightMotor = state.maxRight;
        }
        else if (state.count > 0) {
            out.leftMotor = static_cast<std::uint8_t>(
                std::clamp(state.sumLeft / static_cast<float>(state.count), 0.0f, 255.0f));
            out.rightMotor = static_cast<std::uint8_t>(
                std::clamp(state.sumRight / static_cast<float>(state.count), 0.0f, 255.0f));
        }

        out.confidence = state.maxConfidence;
        out.priority = state.maxPriority;
        out.eventType = state.dominantEvent;
        out.sourceType = state.dominantSource;
        out.qpc = state.latestQpc;
        out.qpcTarget = state.latestTargetQpc;
        out.foregroundHint = state.foregroundHint;
        return out;
    }

    std::uint32_t HidOutput::PercentileOf(std::vector<std::uint32_t> values, float p)
    {
        if (values.empty()) {
            return 0;
        }

        const float clamped = std::clamp(p, 0.0f, 1.0f);
        std::sort(values.begin(), values.end());
        const std::size_t idx = static_cast<std::size_t>(
            clamped * static_cast<float>(values.size() - 1));
        return values[idx];
    }

    void HidOutput::PushLatencySample(std::uint32_t latencyUs, std::uint32_t jitterUs)
    {
        std::scoped_lock lock(_sampleMutex);
        if (_latencySamplesUs.size() >= kLatencySampleCap) {
            _latencySamplesUs.pop_front();
        }
        if (_jitterSamplesUs.size() >= kLatencySampleCap) {
            _jitterSamplesUs.pop_front();
        }
        _latencySamplesUs.push_back(latencyUs);
        _jitterSamplesUs.push_back(jitterUs);
    }

    void HidOutput::PushTrackTimingSample(std::uint32_t renderOverUs, std::optional<std::uint32_t> firstRenderUs)
    {
        std::scoped_lock lock(_sampleMutex);
        if (_trackRenderOverSamplesUs.size() >= kLatencySampleCap) {
            _trackRenderOverSamplesUs.pop_front();
        }
        _trackRenderOverSamplesUs.push_back(renderOverUs);

        if (firstRenderUs.has_value()) {
            if (_trackFirstRenderSamplesUs.size() >= kLatencySampleCap) {
                _trackFirstRenderSamplesUs.pop_front();
            }
            _trackFirstRenderSamplesUs.push_back(*firstRenderUs);
        }
    }

    void HidOutput::FlushPendingOnReaderThread(hid_device* device)
    {
        if (!device) {
            return;
        }

        {
            std::scoped_lock lock(_deviceMutex);
            _device = device;
        }

        const auto& cfg = HapticsConfig::GetSingleton();
        if (cfg.enableStateTrackScheduler) {
            FlushStateTrackOnReaderThread(device, cfg);
            return;
        }
        const auto maxSendPerFlush = std::clamp<std::uint32_t>(cfg.hidMaxSendPerFlush, 1u, 8u);
        std::uint32_t sentThisFlush = 0;
        struct SlotProbeSample
        {
            bool valid{ false };
            std::uint64_t seq{ 0 };
            EventType eventType{ EventType::Unknown };
            SourceType sourceType{ SourceType::AudioMod };
            bool hint{ false };
            std::uint8_t priority{ 0 };
            float confidence{ 0.0f };
            std::uint8_t leftMotor{ 0 };
            std::uint8_t rightMotor{ 0 };
            std::uint64_t targetUs{ 0 };
            std::uint64_t aheadUs{ 0 };
            std::uint64_t overdueUs{ 0 };
            std::uint64_t targetLeadUs{ 0 };
            std::uint64_t slotAgeUs{ 0 };
            std::uint64_t expireInUs{ 0 };
        };

        struct StateProbeSnapshot
        {
            std::uint32_t fgDepth{ 0 };
            std::uint32_t bgDepth{ 0 };
            std::uint32_t dueFg{ 0 };
            std::uint32_t dueBg{ 0 };
            std::uint64_t headOverdueFgUs{ 0 };
            std::uint64_t headOverdueBgUs{ 0 };
            std::uint64_t headFutureFgUs{ 0 };
            std::uint64_t headFutureBgUs{ 0 };
            SlotProbeSample fgSample{};
            SlotProbeSample bgSample{};
        };
        auto snapshotState = [&](std::uint64_t nowUs) -> StateProbeSnapshot {
            StateProbeSnapshot snap{};
            const auto lookaheadUs = static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs);
            std::scoped_lock lock(_stateMutex);

            auto inspectSlot = [&](OutputStateSlot& slot,
                std::uint32_t& depth,
                std::uint32_t& dueCount,
                std::uint64_t& headOverdueUs,
                std::uint64_t& headFutureUs,
                SlotProbeSample& sample) {
                    if (!slot.valid) {
                        return;
                    }
                    if (nowUs > slot.expireUs) {
                        slot.valid = false;
                        _txStateExpiredDrop.fetch_add(1, std::memory_order_relaxed);
                        return;
                    }

                    depth += 1;
                    const auto targetUs = (slot.frame.qpcTarget != 0) ? slot.frame.qpcTarget : slot.frame.qpc;
                    const auto targetLeadUs = (targetUs >= slot.frame.qpc) ? (targetUs - slot.frame.qpc) : 0ull;
                    const auto slotAgeUs = (nowUs > slot.updatedUs) ? (nowUs - slot.updatedUs) : 0ull;
                    const auto expireInUs = (slot.expireUs > nowUs) ? (slot.expireUs - nowUs) : 0ull;
                    std::uint64_t aheadUs = 0;
                    std::uint64_t overdueUs = 0;
                    if (targetUs <= (nowUs + lookaheadUs)) {
                        dueCount += 1;
                    }
                    if (nowUs > targetUs) {
                        overdueUs = nowUs - targetUs;
                        headOverdueUs = std::max(headOverdueUs, overdueUs);
                    }
                    else {
                        aheadUs = targetUs - nowUs;
                        if (headFutureUs == 0 || aheadUs < headFutureUs) {
                            headFutureUs = aheadUs;
                        }
                    }

                    const auto sampleScore = std::max(aheadUs, overdueUs);
                    const auto prevScore = std::max(sample.aheadUs, sample.overdueUs);
                    if (!sample.valid || sampleScore >= prevScore) {
                        sample.valid = true;
                        sample.seq = slot.frame.seq;
                        sample.eventType = slot.frame.eventType;
                        sample.sourceType = slot.frame.sourceType;
                        sample.hint = slot.frame.foregroundHint;
                        sample.priority = slot.frame.priority;
                        sample.confidence = slot.frame.confidence;
                        sample.leftMotor = slot.frame.leftMotor;
                        sample.rightMotor = slot.frame.rightMotor;
                        sample.targetUs = targetUs;
                        sample.targetLeadUs = targetLeadUs;
                        sample.slotAgeUs = slotAgeUs;
                        sample.expireInUs = expireInUs;
                        sample.aheadUs = aheadUs;
                        sample.overdueUs = overdueUs;
                    }
                };

            inspectSlot(_fgState, snap.fgDepth, snap.dueFg, snap.headOverdueFgUs, snap.headFutureFgUs, snap.fgSample);
            inspectSlot(_fgCarryState, snap.fgDepth, snap.dueFg, snap.headOverdueFgUs, snap.headFutureFgUs, snap.fgSample);
            inspectSlot(_bgState, snap.bgDepth, snap.dueBg, snap.headOverdueBgUs, snap.headFutureBgUs, snap.bgSample);
            inspectSlot(_bgCarryState, snap.bgDepth, snap.dueBg, snap.headOverdueBgUs, snap.headFutureBgUs, snap.bgSample);
            _txQueueDepthFg.store(snap.fgDepth, std::memory_order_relaxed);
            _txQueueDepthBg.store(snap.bgDepth, std::memory_order_relaxed);
            return snap;
        };

        struct NoSelectProbeWindow
        {
            std::uint64_t windowStartUs{ 0 };
            std::uint32_t count{ 0 };
            std::uint32_t lookMiss{ 0 };
            std::uint32_t fgFutureOnly{ 0 };
            std::uint32_t bgFutureOnly{ 0 };
            std::uint32_t bothFuture{ 0 };
            std::uint32_t dueButUnselected{ 0 };
            std::uint32_t maxDepthFg{ 0 };
            std::uint32_t maxDepthBg{ 0 };
            std::uint64_t maxHeadFutureFgUs{ 0 };
            std::uint64_t maxHeadFutureBgUs{ 0 };
            std::uint64_t maxHeadOverdueFgUs{ 0 };
            std::uint64_t maxHeadOverdueBgUs{ 0 };
            std::uint32_t gapFgCount{ 0 };
            std::uint32_t gapBgCount{ 0 };
            std::uint64_t gapFgSumUs{ 0 };
            std::uint64_t gapBgSumUs{ 0 };
            std::uint64_t maxGapFgUs{ 0 };
            std::uint64_t maxGapBgUs{ 0 };
            std::uint32_t gapNearCount{ 0 };
            std::uint32_t gapMidCount{ 0 };
            std::uint32_t gapFarCount{ 0 };
            std::uint32_t maxSentThisFlush{ 0 };
            std::uint32_t maxSendPerFlush{ 0 };
            bool hasSample{ false };
            bool sampleForeground{ false };
            std::uint64_t sampleSeq{ 0 };
            EventType sampleEvent{ EventType::Unknown };
            SourceType sampleSource{ SourceType::AudioMod };
            bool sampleHint{ false };
            std::uint8_t samplePriority{ 0 };
            float sampleConfidence{ 0.0f };
            std::uint8_t sampleLeft{ 0 };
            std::uint8_t sampleRight{ 0 };
            std::uint64_t sampleAheadUs{ 0 };
            std::uint64_t sampleOverdueUs{ 0 };
            std::uint64_t sampleTargetUs{ 0 };
            std::uint64_t sampleGapUs{ 0 };
            std::uint64_t sampleTargetLeadUs{ 0 };
            std::uint64_t sampleSlotAgeUs{ 0 };
            std::uint64_t sampleExpireInUs{ 0 };
        };

        struct CapHitProbeWindow
        {
            std::uint64_t windowStartUs{ 0 };
            std::uint32_t count{ 0 };
            std::uint32_t dueFgSum{ 0 };
            std::uint32_t dueBgSum{ 0 };
            std::uint32_t maxDueFg{ 0 };
            std::uint32_t maxDueBg{ 0 };
            std::uint32_t maxDepthFg{ 0 };
            std::uint32_t maxDepthBg{ 0 };
            std::uint64_t maxHeadOverdueFgUs{ 0 };
            std::uint64_t maxHeadOverdueBgUs{ 0 };
            std::uint32_t maxSentThisFlush{ 0 };
            std::uint32_t maxSendPerFlush{ 0 };
            bool hasSample{ false };
            bool sampleForeground{ false };
            std::uint64_t sampleSeq{ 0 };
            EventType sampleEvent{ EventType::Unknown };
            SourceType sampleSource{ SourceType::AudioMod };
            bool sampleHint{ false };
            std::uint8_t samplePriority{ 0 };
            float sampleConfidence{ 0.0f };
            std::uint8_t sampleLeft{ 0 };
            std::uint8_t sampleRight{ 0 };
            std::uint64_t sampleAheadUs{ 0 };
            std::uint64_t sampleOverdueUs{ 0 };
            std::uint64_t sampleTargetUs{ 0 };
        };

        enum class StaleAction : std::uint8_t
        {
            HardDrop = 0,
            SoftSalvage,
            SalvageZeroDrop
        };

        struct StaleProbeWindow
        {
            std::uint64_t windowStartUs{ 0 };
            std::uint32_t hardDropCount{ 0 };
            std::uint32_t softSalvageCount{ 0 };
            std::uint32_t salvageZeroDropCount{ 0 };
            std::uint32_t repeatedSeqDropCount{ 0 };
            std::uint64_t maxOverdueUs{ 0 };
            std::uint32_t maxDepthFg{ 0 };
            std::uint32_t maxDepthBg{ 0 };
            std::uint64_t lastDropSeq{ 0 };
            bool hasSample{ false };
            StaleAction sampleAction{ StaleAction::HardDrop };
            bool sampleForeground{ false };
            RouteReason sampleReason{ RouteReason::EventForeground };
            EventType sampleEvent{ EventType::Unknown };
            SourceType sampleSource{ SourceType::AudioMod };
            bool sampleHint{ false };
            std::uint8_t samplePriority{ 0 };
            float sampleConfidence{ 0.0f };
            std::uint8_t sampleLeft{ 0 };
            std::uint8_t sampleRight{ 0 };
            std::uint64_t sampleOverdueUs{ 0 };
            std::uint64_t sampleSoftWindowUs{ 0 };
            float sampleGain{ 1.0f };
        };

        struct RepeatGuardProbeWindow
        {
            std::uint64_t windowStartUs{ 0 };
            std::uint32_t count{ 0 };
            std::uint32_t fgCount{ 0 };
            std::uint32_t bgCount{ 0 };
            std::uint32_t zeroCount{ 0 };
            std::uint32_t nonZeroCount{ 0 };
            std::uint64_t maxDeltaUs{ 0 };
            std::uint64_t maxMinRepeatUs{ 0 };
            std::uint32_t maxDepthFg{ 0 };
            std::uint32_t maxDepthBg{ 0 };
            bool hasSample{ false };
            bool sampleForeground{ false };
            std::uint64_t sampleSeq{ 0 };
            EventType sampleEvent{ EventType::Unknown };
            SourceType sampleSource{ SourceType::AudioMod };
            bool sampleHint{ false };
            std::uint8_t samplePriority{ 0 };
            float sampleConfidence{ 0.0f };
            std::uint8_t sampleLeft{ 0 };
            std::uint8_t sampleRight{ 0 };
            std::uint64_t sampleDeltaUs{ 0 };
            std::uint64_t sampleMinRepeatUs{ 0 };
            std::uint64_t sampleAgeUs{ 0 };
            std::uint64_t sampleTargetLeadUs{ 0 };
        };

        auto staleActionLabel = [](StaleAction action) -> const char* {
            switch (action) {
            case StaleAction::HardDrop:
                return "hardDrop";
            case StaleAction::SoftSalvage:
                return "softSalvage";
            case StaleAction::SalvageZeroDrop:
                return "salvageZeroDrop";
            default:
                return "unknown";
            }
        };

        static NoSelectProbeWindow s_noSelectProbe{};
        static CapHitProbeWindow s_capHitProbe{};
        static StaleProbeWindow s_staleProbe{};
        static RepeatGuardProbeWindow s_repeatGuardProbe{};
        static std::atomic<std::uint64_t> s_repeatKeepProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_repeatKeepProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_repeatEvalProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_repeatEvalProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_repeatDropProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_repeatDropProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_noSelectDetailProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_noSelectDetailProbeLines{ 0 };
        static std::atomic<std::uint64_t> s_selectSwitchProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_selectSwitchProbeLines{ 0 };

        auto flushNoSelectProbeWindow = [&](std::uint64_t tsUs) {
            auto& w = s_noSelectProbe;
            if (w.windowStartUs == 0) {
                w.windowStartUs = tsUs;
                return;
            }
            if (tsUs >= w.windowStartUs && (tsUs - w.windowStartUs) < 1000000ull) {
                return;
            }
            if (w.count > 0) {
                const auto avgGapFgUs = (w.gapFgCount > 0) ? (w.gapFgSumUs / w.gapFgCount) : 0ull;
                const auto avgGapBgUs = (w.gapBgCount > 0) ? (w.gapBgSumUs / w.gapBgCount) : 0ull;
                const auto suggestedLookaheadUs = std::max<std::uint64_t>(
                    static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs),
                    std::max(w.maxHeadFutureFgUs, w.maxHeadFutureBgUs) + 120ull);
                logger::info(
                    "[Haptics][ProbeTxSummary] site=flush/no_select branch=no_due_candidate count={} lookMiss={} fgFutureOnly={} bgFutureOnly={} bothFuture={} dueButUnselected={} maxDepthFg={} maxDepthBg={} maxHeadFutureFg={}us maxHeadFutureBg={}us maxHeadOverFg={}us maxHeadOverBg={}us lookahead={}us suggestLookahead={}us gap(avgFg={}us avgBg={}us maxFg={}us maxBg={}us near={} mid={} far={}) maxSent={} maxSend={} sample(fg={} seq={} evt={} src={} hint={} prio={} conf={:.2f} motor={}/{} ahead={}us over={}us gap={}us lead={}us age={}us expireIn={}us target={})",
                    w.count,
                    w.lookMiss,
                    w.fgFutureOnly,
                    w.bgFutureOnly,
                    w.bothFuture,
                    w.dueButUnselected,
                    w.maxDepthFg,
                    w.maxDepthBg,
                    w.maxHeadFutureFgUs,
                    w.maxHeadFutureBgUs,
                    w.maxHeadOverdueFgUs,
                    w.maxHeadOverdueBgUs,
                    cfg.hidSchedulerLookaheadUs,
                    suggestedLookaheadUs,
                    avgGapFgUs,
                    avgGapBgUs,
                    w.maxGapFgUs,
                    w.maxGapBgUs,
                    w.gapNearCount,
                    w.gapMidCount,
                    w.gapFarCount,
                    w.maxSentThisFlush,
                    w.maxSendPerFlush,
                    w.hasSample ? (w.sampleForeground ? 1 : 0) : -1,
                    w.hasSample ? w.sampleSeq : 0ull,
                    w.hasSample ? ToString(w.sampleEvent) : "None",
                    w.hasSample ? static_cast<int>(w.sampleSource) : -1,
                    w.hasSample ? (w.sampleHint ? 1 : 0) : -1,
                    w.hasSample ? static_cast<int>(w.samplePriority) : -1,
                    w.hasSample ? w.sampleConfidence : 0.0f,
                    w.hasSample ? static_cast<int>(w.sampleLeft) : 0,
                    w.hasSample ? static_cast<int>(w.sampleRight) : 0,
                    w.hasSample ? w.sampleAheadUs : 0ull,
                    w.hasSample ? w.sampleOverdueUs : 0ull,
                    w.hasSample ? w.sampleGapUs : 0ull,
                    w.hasSample ? w.sampleTargetLeadUs : 0ull,
                    w.hasSample ? w.sampleSlotAgeUs : 0ull,
                    w.hasSample ? w.sampleExpireInUs : 0ull,
                    w.hasSample ? w.sampleTargetUs : 0ull);
            }
            w = {};
            w.windowStartUs = tsUs;
        };

        auto recordNoSelectProbe = [&](std::uint64_t tsUs, const StateProbeSnapshot& snap, std::uint32_t sent) {
            flushNoSelectProbeWindow(tsUs);
            auto& w = s_noSelectProbe;
            const auto lookaheadUs = static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs);
            auto bucketGap = [&](std::uint64_t gapUs) {
                if (gapUs <= 1000ull) {
                    w.gapNearCount += 1;
                }
                else if (gapUs <= 3000ull) {
                    w.gapMidCount += 1;
                }
                else {
                    w.gapFarCount += 1;
                }
            };
            auto gapToDue = [&](std::uint64_t headFutureUs) {
                return (headFutureUs > lookaheadUs) ? (headFutureUs - lookaheadUs) : 0ull;
            };
            w.count += 1;
            if (snap.dueFg == 0 && snap.dueBg == 0) {
                w.lookMiss += 1;
            }
            if (snap.fgDepth > 0 && snap.bgDepth == 0 && snap.dueFg == 0) {
                w.fgFutureOnly += 1;
            }
            else if (snap.bgDepth > 0 && snap.fgDepth == 0 && snap.dueBg == 0) {
                w.bgFutureOnly += 1;
            }
            else if (snap.fgDepth > 0 && snap.bgDepth > 0 && snap.dueFg == 0 && snap.dueBg == 0) {
                w.bothFuture += 1;
            }
            else if (snap.dueFg > 0 || snap.dueBg > 0) {
                w.dueButUnselected += 1;
            }
            w.maxDepthFg = std::max(w.maxDepthFg, snap.fgDepth);
            w.maxDepthBg = std::max(w.maxDepthBg, snap.bgDepth);
            w.maxHeadFutureFgUs = std::max(w.maxHeadFutureFgUs, snap.headFutureFgUs);
            w.maxHeadFutureBgUs = std::max(w.maxHeadFutureBgUs, snap.headFutureBgUs);
            w.maxHeadOverdueFgUs = std::max(w.maxHeadOverdueFgUs, snap.headOverdueFgUs);
            w.maxHeadOverdueBgUs = std::max(w.maxHeadOverdueBgUs, snap.headOverdueBgUs);
            if (snap.fgDepth > 0 && snap.dueFg == 0 && snap.headFutureFgUs > 0) {
                const auto gapUs = gapToDue(snap.headFutureFgUs);
                w.gapFgCount += 1;
                w.gapFgSumUs += gapUs;
                w.maxGapFgUs = std::max(w.maxGapFgUs, gapUs);
                bucketGap(gapUs);
            }
            if (snap.bgDepth > 0 && snap.dueBg == 0 && snap.headFutureBgUs > 0) {
                const auto gapUs = gapToDue(snap.headFutureBgUs);
                w.gapBgCount += 1;
                w.gapBgSumUs += gapUs;
                w.maxGapBgUs = std::max(w.maxGapBgUs, gapUs);
                bucketGap(gapUs);
            }
            w.maxSentThisFlush = std::max(w.maxSentThisFlush, sent);
            w.maxSendPerFlush = std::max(w.maxSendPerFlush, maxSendPerFlush);

            const SlotProbeSample* sample = nullptr;
            bool sampleForeground = false;
            std::uint64_t sampleAheadOrOverUs = 0;
            if (snap.fgSample.valid) {
                sample = &snap.fgSample;
                sampleForeground = true;
                sampleAheadOrOverUs = std::max(gapToDue(snap.fgSample.aheadUs), snap.fgSample.overdueUs);
            }
            if (snap.bgSample.valid) {
                const auto bgAheadOrOverUs = std::max(gapToDue(snap.bgSample.aheadUs), snap.bgSample.overdueUs);
                if (!sample || bgAheadOrOverUs > sampleAheadOrOverUs) {
                    sample = &snap.bgSample;
                    sampleForeground = false;
                    sampleAheadOrOverUs = bgAheadOrOverUs;
                }
            }

            if (sample && (!w.hasSample || sampleAheadOrOverUs >= std::max(w.sampleAheadUs, w.sampleOverdueUs))) {
                w.hasSample = true;
                w.sampleForeground = sampleForeground;
                w.sampleSeq = sample->seq;
                w.sampleEvent = sample->eventType;
                w.sampleSource = sample->sourceType;
                w.sampleHint = sample->hint;
                w.samplePriority = sample->priority;
                w.sampleConfidence = sample->confidence;
                w.sampleLeft = sample->leftMotor;
                w.sampleRight = sample->rightMotor;
                w.sampleAheadUs = sample->aheadUs;
                w.sampleOverdueUs = sample->overdueUs;
                w.sampleGapUs = gapToDue(sample->aheadUs);
                w.sampleTargetLeadUs = sample->targetLeadUs;
                w.sampleSlotAgeUs = sample->slotAgeUs;
                w.sampleExpireInUs = sample->expireInUs;
                w.sampleTargetUs = sample->targetUs;
            }
        };

        auto flushCapHitProbeWindow = [&](std::uint64_t tsUs) {
            auto& w = s_capHitProbe;
            if (w.windowStartUs == 0) {
                w.windowStartUs = tsUs;
                return;
            }
            if (tsUs >= w.windowStartUs && (tsUs - w.windowStartUs) < 1000000ull) {
                return;
            }
            if (w.count > 0) {
                const auto avgDuePerHit = static_cast<float>(w.dueFgSum + w.dueBgSum) /
                    static_cast<float>(std::max<std::uint32_t>(1u, w.count));
                logger::info(
                    "[Haptics][ProbeTxSummary] site=flush/cap_hit branch=max_send_limit count={} dueFgSum={} dueBgSum={} avgDuePerHit={:.2f} maxDueFg={} maxDueBg={} maxDepthFg={} maxDepthBg={} maxHeadOverFg={}us maxHeadOverBg={}us stale={}us lookahead={}us maxSent={} maxSend={} sample(fg={} seq={} evt={} src={} hint={} prio={} conf={:.2f} motor={}/{} ahead={}us over={}us target={})",
                    w.count,
                    w.dueFgSum,
                    w.dueBgSum,
                    avgDuePerHit,
                    w.maxDueFg,
                    w.maxDueBg,
                    w.maxDepthFg,
                    w.maxDepthBg,
                    w.maxHeadOverdueFgUs,
                    w.maxHeadOverdueBgUs,
                    cfg.hidStaleUs,
                    cfg.hidSchedulerLookaheadUs,
                    w.maxSentThisFlush,
                    w.maxSendPerFlush,
                    w.hasSample ? (w.sampleForeground ? 1 : 0) : -1,
                    w.hasSample ? w.sampleSeq : 0ull,
                    w.hasSample ? ToString(w.sampleEvent) : "None",
                    w.hasSample ? static_cast<int>(w.sampleSource) : -1,
                    w.hasSample ? (w.sampleHint ? 1 : 0) : -1,
                    w.hasSample ? static_cast<int>(w.samplePriority) : -1,
                    w.hasSample ? w.sampleConfidence : 0.0f,
                    w.hasSample ? static_cast<int>(w.sampleLeft) : 0,
                    w.hasSample ? static_cast<int>(w.sampleRight) : 0,
                    w.hasSample ? w.sampleAheadUs : 0ull,
                    w.hasSample ? w.sampleOverdueUs : 0ull,
                    w.hasSample ? w.sampleTargetUs : 0ull);
            }
            w = {};
            w.windowStartUs = tsUs;
        };

        auto recordCapHitProbe = [&](std::uint64_t tsUs, const StateProbeSnapshot& snap, std::uint32_t sent) {
            flushCapHitProbeWindow(tsUs);
            auto& w = s_capHitProbe;
            w.count += 1;
            w.dueFgSum += snap.dueFg;
            w.dueBgSum += snap.dueBg;
            w.maxDueFg = std::max(w.maxDueFg, snap.dueFg);
            w.maxDueBg = std::max(w.maxDueBg, snap.dueBg);
            w.maxDepthFg = std::max(w.maxDepthFg, snap.fgDepth);
            w.maxDepthBg = std::max(w.maxDepthBg, snap.bgDepth);
            w.maxHeadOverdueFgUs = std::max(w.maxHeadOverdueFgUs, snap.headOverdueFgUs);
            w.maxHeadOverdueBgUs = std::max(w.maxHeadOverdueBgUs, snap.headOverdueBgUs);
            w.maxSentThisFlush = std::max(w.maxSentThisFlush, sent);
            w.maxSendPerFlush = std::max(w.maxSendPerFlush, maxSendPerFlush);

            const SlotProbeSample* sample = nullptr;
            bool sampleForeground = false;
            std::uint64_t sampleOverdueOrAheadUs = 0;
            if (snap.fgSample.valid && snap.dueFg > 0) {
                sample = &snap.fgSample;
                sampleForeground = true;
                sampleOverdueOrAheadUs = std::max(snap.fgSample.overdueUs, snap.fgSample.aheadUs);
            }
            if (snap.bgSample.valid && snap.dueBg > 0) {
                const auto bgOverdueOrAheadUs = std::max(snap.bgSample.overdueUs, snap.bgSample.aheadUs);
                if (!sample || bgOverdueOrAheadUs > sampleOverdueOrAheadUs) {
                    sample = &snap.bgSample;
                    sampleForeground = false;
                    sampleOverdueOrAheadUs = bgOverdueOrAheadUs;
                }
            }

            if (sample && (!w.hasSample || sampleOverdueOrAheadUs >= std::max(w.sampleOverdueUs, w.sampleAheadUs))) {
                w.hasSample = true;
                w.sampleForeground = sampleForeground;
                w.sampleSeq = sample->seq;
                w.sampleEvent = sample->eventType;
                w.sampleSource = sample->sourceType;
                w.sampleHint = sample->hint;
                w.samplePriority = sample->priority;
                w.sampleConfidence = sample->confidence;
                w.sampleLeft = sample->leftMotor;
                w.sampleRight = sample->rightMotor;
                w.sampleAheadUs = sample->aheadUs;
                w.sampleOverdueUs = sample->overdueUs;
                w.sampleTargetUs = sample->targetUs;
            }
        };

        auto flushStaleProbeWindow = [&](std::uint64_t tsUs) {
            auto& w = s_staleProbe;
            if (w.windowStartUs == 0) {
                w.windowStartUs = tsUs;
                return;
            }
            if (tsUs >= w.windowStartUs && (tsUs - w.windowStartUs) < 1000000ull) {
                return;
            }
            if ((w.hardDropCount + w.softSalvageCount + w.salvageZeroDropCount) > 0) {
                logger::info(
                    "[Haptics][ProbeTxSummary] site=flush/stale branch=hard_drop_or_salvage drop={} salvage={} salvageZeroDrop={} repeatSeqDrop={} maxOverdue={}us sample(action={} fg={} reason={} over={}us gain={:.2f}) maxDepthFg={} maxDepthBg={} stale={}us lookahead={}us",
                    w.hardDropCount,
                    w.softSalvageCount,
                    w.salvageZeroDropCount,
                    w.repeatedSeqDropCount,
                    w.maxOverdueUs,
                    staleActionLabel(w.sampleAction),
                    w.sampleForeground ? 1 : 0,
                    RouteReasonLabel(w.sampleReason),
                    w.sampleOverdueUs,
                    w.sampleGain,
                    w.maxDepthFg,
                    w.maxDepthBg,
                    cfg.hidStaleUs,
                    cfg.hidSchedulerLookaheadUs);
            }
            w = {};
            w.windowStartUs = tsUs;
        };

        auto recordStaleProbe = [&](std::uint64_t tsUs,
                                    StaleAction action,
                                    const HidFrame& frame,
                                    bool foreground,
                                    RouteReason reason,
                                    std::uint64_t overdueUs,
                                    std::uint64_t softWindowUs,
                                    float gain) {
            flushStaleProbeWindow(tsUs);
            auto& w = s_staleProbe;
            switch (action) {
            case StaleAction::HardDrop:
                w.hardDropCount += 1;
                break;
            case StaleAction::SoftSalvage:
                w.softSalvageCount += 1;
                break;
            case StaleAction::SalvageZeroDrop:
                w.hardDropCount += 1;
                w.salvageZeroDropCount += 1;
                break;
            default:
                break;
            }

            if ((action == StaleAction::HardDrop || action == StaleAction::SalvageZeroDrop) && frame.seq != 0) {
                if (w.lastDropSeq == frame.seq) {
                    w.repeatedSeqDropCount += 1;
                }
                w.lastDropSeq = frame.seq;
            }

            w.maxDepthFg = std::max(w.maxDepthFg, _txQueueDepthFg.load(std::memory_order_relaxed));
            w.maxDepthBg = std::max(w.maxDepthBg, _txQueueDepthBg.load(std::memory_order_relaxed));

            if (!w.hasSample || overdueUs >= w.maxOverdueUs) {
                w.hasSample = true;
                w.maxOverdueUs = overdueUs;
                w.sampleAction = action;
                w.sampleForeground = foreground;
                w.sampleReason = reason;
                w.sampleEvent = frame.eventType;
                w.sampleSource = frame.sourceType;
                w.sampleHint = frame.foregroundHint;
                w.samplePriority = frame.priority;
                w.sampleConfidence = frame.confidence;
                w.sampleLeft = frame.leftMotor;
                w.sampleRight = frame.rightMotor;
                w.sampleOverdueUs = overdueUs;
                w.sampleSoftWindowUs = softWindowUs;
                w.sampleGain = gain;
            }
        };

        auto flushRepeatGuardProbeWindow = [&](std::uint64_t tsUs) {
            auto& w = s_repeatGuardProbe;
            if (w.windowStartUs == 0) {
                w.windowStartUs = tsUs;
                return;
            }
            if (tsUs >= w.windowStartUs && (tsUs - w.windowStartUs) < 1000000ull) {
                return;
            }
            if (w.count > 0) {
                logger::info(
                    "[Haptics][ProbeTxSummary] site=flush/repeat_guard branch=same_output_min_interval count={} fg={} bg={} zero={} nonZero={} maxDelta={}us maxMinReq={}us maxDepthFg={} maxDepthBg={} sample(fg={} seq={} evt={} src={} hint={} prio={} conf={:.2f} motor={}/{} delta={}us minReq={}us age={}us lead={}us)",
                    w.count,
                    w.fgCount,
                    w.bgCount,
                    w.zeroCount,
                    w.nonZeroCount,
                    w.maxDeltaUs,
                    w.maxMinRepeatUs,
                    w.maxDepthFg,
                    w.maxDepthBg,
                    w.hasSample ? (w.sampleForeground ? 1 : 0) : -1,
                    w.hasSample ? w.sampleSeq : 0ull,
                    w.hasSample ? ToString(w.sampleEvent) : "None",
                    w.hasSample ? static_cast<int>(w.sampleSource) : -1,
                    w.hasSample ? (w.sampleHint ? 1 : 0) : -1,
                    w.hasSample ? static_cast<int>(w.samplePriority) : -1,
                    w.hasSample ? w.sampleConfidence : 0.0f,
                    w.hasSample ? static_cast<int>(w.sampleLeft) : 0,
                    w.hasSample ? static_cast<int>(w.sampleRight) : 0,
                    w.hasSample ? w.sampleDeltaUs : 0ull,
                    w.hasSample ? w.sampleMinRepeatUs : 0ull,
                    w.hasSample ? w.sampleAgeUs : 0ull,
                    w.hasSample ? w.sampleTargetLeadUs : 0ull);
            }
            w = {};
            w.windowStartUs = tsUs;
        };

        auto recordRepeatGuardProbe = [&](std::uint64_t tsUs,
                                          const HidFrame& frame,
                                          bool foreground,
                                          std::uint64_t deltaUs,
                                          std::uint64_t minRepeatUs) {
            flushRepeatGuardProbeWindow(tsUs);
            auto& w = s_repeatGuardProbe;
            w.count += 1;
            if (foreground) {
                w.fgCount += 1;
            }
            else {
                w.bgCount += 1;
            }
            if (frame.leftMotor == 0 && frame.rightMotor == 0) {
                w.zeroCount += 1;
            }
            else {
                w.nonZeroCount += 1;
            }
            w.maxDeltaUs = std::max(w.maxDeltaUs, deltaUs);
            w.maxMinRepeatUs = std::max(w.maxMinRepeatUs, minRepeatUs);
            w.maxDepthFg = std::max(w.maxDepthFg, _txQueueDepthFg.load(std::memory_order_relaxed));
            w.maxDepthBg = std::max(w.maxDepthBg, _txQueueDepthBg.load(std::memory_order_relaxed));

            const auto score = std::max(deltaUs, minRepeatUs);
            const auto prevScore = std::max(w.sampleDeltaUs, w.sampleMinRepeatUs);
            if (!w.hasSample || score >= prevScore) {
                w.hasSample = true;
                w.sampleForeground = foreground;
                w.sampleSeq = frame.seq;
                w.sampleEvent = frame.eventType;
                w.sampleSource = frame.sourceType;
                w.sampleHint = frame.foregroundHint;
                w.samplePriority = frame.priority;
                w.sampleConfidence = frame.confidence;
                w.sampleLeft = frame.leftMotor;
                w.sampleRight = frame.rightMotor;
                w.sampleDeltaUs = deltaUs;
                w.sampleMinRepeatUs = minRepeatUs;
                w.sampleAgeUs = (tsUs > frame.qpc) ? (tsUs - frame.qpc) : 0ull;
                w.sampleTargetLeadUs = (frame.qpcTarget >= frame.qpc) ? (frame.qpcTarget - frame.qpc) : 0ull;
            }
        };

        enum class SelectedStateSlot : std::uint8_t
        {
            None = 0,
            ForegroundPrimary,
            ForegroundCarry,
            BackgroundPrimary,
            BackgroundCarry
        };

        auto stateSlotById = [&](SelectedStateSlot slot) -> OutputStateSlot* {
            switch (slot) {
            case SelectedStateSlot::ForegroundPrimary:
                return &_fgState;
            case SelectedStateSlot::ForegroundCarry:
                return &_fgCarryState;
            case SelectedStateSlot::BackgroundPrimary:
                return &_bgState;
            case SelectedStateSlot::BackgroundCarry:
                return &_bgCarryState;
            default:
                return nullptr;
            }
        };

        auto slotLabelById = [](SelectedStateSlot slot) -> const char* {
            switch (slot) {
            case SelectedStateSlot::ForegroundPrimary:
                return "fgPrimary";
            case SelectedStateSlot::ForegroundCarry:
                return "fgCarry";
            case SelectedStateSlot::BackgroundPrimary:
                return "bgPrimary";
            case SelectedStateSlot::BackgroundCarry:
                return "bgCarry";
            default:
                return "none";
            }
        };

        auto consumeSelectedStateSlot = [&](SelectedStateSlot slot, std::uint64_t seq) {
            if (slot == SelectedStateSlot::None || seq == 0) {
                return;
            }

            std::scoped_lock lock(_stateMutex);
            OutputStateSlot* stateSlot = stateSlotById(slot);

            if (!stateSlot || !stateSlot->valid) {
                _txQueueDepthFg.store((_fgState.valid ? 1u : 0u) + (_fgCarryState.valid ? 1u : 0u), std::memory_order_relaxed);
                _txQueueDepthBg.store((_bgState.valid ? 1u : 0u) + (_bgCarryState.valid ? 1u : 0u), std::memory_order_relaxed);
                return;
            }

            // Consume only if this is still the same slot payload; newer overwrite wins.
            if (stateSlot->frame.seq == seq) {
                stateSlot->valid = false;
                if (slot == SelectedStateSlot::ForegroundCarry) {
                    _txStateCarryConsumedFg.fetch_add(1, std::memory_order_relaxed);
                }
                else if (slot == SelectedStateSlot::BackgroundCarry) {
                    _txStateCarryConsumedBg.fetch_add(1, std::memory_order_relaxed);
                }
            }

            _txQueueDepthFg.store((_fgState.valid ? 1u : 0u) + (_fgCarryState.valid ? 1u : 0u), std::memory_order_relaxed);
            _txQueueDepthBg.store((_bgState.valid ? 1u : 0u) + (_bgCarryState.valid ? 1u : 0u), std::memory_order_relaxed);
        };

        while (sentThisFlush < maxSendPerFlush) {
            const auto nowUs = ToQPC(Now());
            HidFrame selected{};
            bool selectedForeground = false;
            bool hasSelected = false;
            SelectedStateSlot selectedSlot = SelectedStateSlot::None;
            auto snap = snapshotState(nowUs);

            {
                std::scoped_lock lock(_stateMutex);
                const auto dueWindowUs = nowUs + static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs);
                std::uint64_t bestTargetUs = 0;
                auto isPrimarySlot = [](SelectedStateSlot slot) {
                    return slot == SelectedStateSlot::ForegroundPrimary ||
                        slot == SelectedStateSlot::BackgroundPrimary;
                };

                auto consider = [&](OutputStateSlot& stateSlot, SelectedStateSlot slotId, bool isForeground) {
                    if (!stateSlot.valid) {
                        return;
                    }
                    if (nowUs > stateSlot.expireUs) {
                        stateSlot.valid = false;
                        _txStateExpiredDrop.fetch_add(1, std::memory_order_relaxed);
                        return;
                    }

                    const auto targetUs = (stateSlot.frame.qpcTarget != 0) ?
                        stateSlot.frame.qpcTarget :
                        stateSlot.frame.qpc;
                    if (targetUs == 0 || targetUs > dueWindowUs) {
                        return;
                    }

                    if (!hasSelected) {
                        selected = stateSlot.frame;
                        selectedForeground = isForeground;
                        selectedSlot = slotId;
                        bestTargetUs = targetUs;
                        hasSelected = true;
                        return;
                    }

                    bool choose = false;
                    const char* chooseReason = "none";
                    if (targetUs < bestTargetUs) {
                        choose = true;
                        chooseReason = "earlier_target";
                    }
                    else if (targetUs == bestTargetUs) {
                        if (isForeground && !selectedForeground) {
                            choose = true;
                            chooseReason = "tiebreak_fg_over_bg";
                        }
                        else if (isForeground == selectedForeground &&
                                 isPrimarySlot(slotId) &&
                                 !isPrimarySlot(selectedSlot)) {
                            choose = true;
                            chooseReason = "tiebreak_primary_over_carry";
                        }
                    }

                    if (choose) {
                        const auto prevTargetUs = bestTargetUs;
                        const auto prevSlot = selectedSlot;
                        const auto prevSeq = selected.seq;
                        const auto prevEvt = selected.eventType;
                        selected = stateSlot.frame;
                        selectedForeground = isForeground;
                        selectedSlot = slotId;
                        bestTargetUs = targetUs;
                        if (ShouldEmitWindowedProbe(
                                s_selectSwitchProbeWindowUs,
                                s_selectSwitchProbeLines,
                                nowUs,
                                12)) {
                            logger::info(
                                "[Haptics][ProbeTxDetail] site=flush/select_switch reason={} prev(slot={} seq={} evt={} target={}) next(slot={} seq={} evt={} target={}) dueWindow={}us delta={}us",
                                chooseReason,
                                slotLabelById(prevSlot),
                                prevSeq,
                                ToString(prevEvt),
                                prevTargetUs,
                                slotLabelById(slotId),
                                stateSlot.frame.seq,
                                ToString(stateSlot.frame.eventType),
                                targetUs,
                                cfg.hidSchedulerLookaheadUs,
                                (prevTargetUs > targetUs) ? (prevTargetUs - targetUs) : (targetUs - prevTargetUs));
                        }
                    }
                };

                consider(_fgState, SelectedStateSlot::ForegroundPrimary, true);
                consider(_fgCarryState, SelectedStateSlot::ForegroundCarry, true);
                consider(_bgState, SelectedStateSlot::BackgroundPrimary, false);
                consider(_bgCarryState, SelectedStateSlot::BackgroundCarry, false);
            }

            if (!hasSelected) {
                if (snap.fgDepth > 0 || snap.bgDepth > 0) {
                    _txFlushNoSelectPending.fetch_add(1, std::memory_order_relaxed);
                    if (snap.dueFg == 0 && snap.dueBg == 0) {
                        _txStateFutureSkip.fetch_add(1, std::memory_order_relaxed);
                        _txFlushLookaheadMiss.fetch_add(1, std::memory_order_relaxed);
                    }
                    recordNoSelectProbe(nowUs, snap, sentThisFlush);

                    if (ShouldEmitWindowedProbe(
                            s_noSelectDetailProbeWindowUs,
                            s_noSelectDetailProbeLines,
                            nowUs,
                            8)) {
                        struct SlotDetail
                        {
                            bool valid{ false };
                            bool due{ false };
                            std::uint64_t seq{ 0 };
                            EventType evt{ EventType::Unknown };
                            std::uint64_t targetUs{ 0 };
                            std::uint64_t aheadUs{ 0 };
                            std::uint64_t overUs{ 0 };
                            std::uint64_t leadUs{ 0 };
                        };
                        auto readSlotDetail = [&](const OutputStateSlot& slot, std::uint64_t dueWindowUs) {
                            SlotDetail d{};
                            if (!slot.valid) {
                                return d;
                            }
                            d.valid = true;
                            d.seq = slot.frame.seq;
                            d.evt = slot.frame.eventType;
                            d.targetUs = (slot.frame.qpcTarget != 0) ? slot.frame.qpcTarget : slot.frame.qpc;
                            d.due = d.targetUs != 0 && d.targetUs <= dueWindowUs;
                            d.aheadUs = (d.targetUs > nowUs) ? (d.targetUs - nowUs) : 0ull;
                            d.overUs = (nowUs > d.targetUs) ? (nowUs - d.targetUs) : 0ull;
                            d.leadUs = (slot.frame.qpcTarget >= slot.frame.qpc) ?
                                (slot.frame.qpcTarget - slot.frame.qpc) :
                                0ull;
                            return d;
                        };

                        SlotDetail fgPri{};
                        SlotDetail fgCar{};
                        SlotDetail bgPri{};
                        SlotDetail bgCar{};
                        const auto dueWindowUs = nowUs + static_cast<std::uint64_t>(cfg.hidSchedulerLookaheadUs);
                        {
                            std::scoped_lock lock(_stateMutex);
                            fgPri = readSlotDetail(_fgState, dueWindowUs);
                            fgCar = readSlotDetail(_fgCarryState, dueWindowUs);
                            bgPri = readSlotDetail(_bgState, dueWindowUs);
                            bgCar = readSlotDetail(_bgCarryState, dueWindowUs);
                        }

                        logger::info(
                            "[Haptics][ProbeTxDetail] site=flush/no_select_detail now={} lookahead={}us fgDepth={} bgDepth={} dueFg={} dueBg={} "
                            "fgPri(v={} due={} seq={} evt={} target={} ahead={}us over={}us lead={}us) "
                            "fgCar(v={} due={} seq={} evt={} target={} ahead={}us over={}us lead={}us) "
                            "bgPri(v={} due={} seq={} evt={} target={} ahead={}us over={}us lead={}us) "
                            "bgCar(v={} due={} seq={} evt={} target={} ahead={}us over={}us lead={}us)",
                            nowUs,
                            cfg.hidSchedulerLookaheadUs,
                            snap.fgDepth,
                            snap.bgDepth,
                            snap.dueFg,
                            snap.dueBg,
                            fgPri.valid ? 1 : 0,
                            fgPri.due ? 1 : 0,
                            fgPri.seq,
                            ToString(fgPri.evt),
                            fgPri.targetUs,
                            fgPri.aheadUs,
                            fgPri.overUs,
                            fgPri.leadUs,
                            fgCar.valid ? 1 : 0,
                            fgCar.due ? 1 : 0,
                            fgCar.seq,
                            ToString(fgCar.evt),
                            fgCar.targetUs,
                            fgCar.aheadUs,
                            fgCar.overUs,
                            fgCar.leadUs,
                            bgPri.valid ? 1 : 0,
                            bgPri.due ? 1 : 0,
                            bgPri.seq,
                            ToString(bgPri.evt),
                            bgPri.targetUs,
                            bgPri.aheadUs,
                            bgPri.overUs,
                            bgPri.leadUs,
                            bgCar.valid ? 1 : 0,
                            bgCar.due ? 1 : 0,
                            bgCar.seq,
                            ToString(bgCar.evt),
                            bgCar.targetUs,
                            bgCar.aheadUs,
                            bgCar.overUs,
                            bgCar.leadUs);
                    }
                }

                if (_lastSentValid &&
                    (_lastSentLeft != 0 || _lastSentRight != 0) &&
                    (_lastSendQpc == 0 || nowUs >= (_lastSendQpc + static_cast<std::uint64_t>(cfg.hidIdleRepeatIntervalUs)))) {
                    HidFrame stop{};
                    stop.qpc = nowUs;
                    stop.qpcTarget = nowUs;
                    stop.eventType = EventType::Unknown;
                    stop.sourceType = SourceType::AudioMod;
                    stop.priority = 255;
                    stop.foregroundHint = true;
                    selected = stop;
                    selectedForeground = true;
                    hasSelected = true;
                }
            }

            if (!hasSelected) {
                break;
            }

            const auto targetUs = selected.qpcTarget != 0 ? selected.qpcTarget : selected.qpc;
            if (targetUs != 0 && nowUs > targetUs && (nowUs - targetUs) > cfg.hidStaleUs) {
                const auto overdueUs = nowUs - targetUs;
                const bool hasStructuredForegroundHint =
                    selectedForeground &&
                    (selected.eventType != EventType::Unknown ||
                        selected.foregroundHint ||
                        selected.priority >= static_cast<std::uint8_t>(std::max(0, cfg.prioritySwing)));
                const auto softWindowUs = hasStructuredForegroundHint ?
                    std::clamp<std::uint64_t>(
                        static_cast<std::uint64_t>(cfg.hidStaleUs / 2u) + 6000ull,
                        6000ull,
                        18000ull) :
                    0ull;
                const bool allowSoftSalvage =
                    hasStructuredForegroundHint &&
                    overdueUs <= (static_cast<std::uint64_t>(cfg.hidStaleUs) + softWindowUs);
                RouteReason staleReason = RouteReason::EventForeground;
                (void)ClassifyForegroundFrame(selected, cfg.prioritySwing, staleReason);

                if (!allowSoftSalvage) {
                    if (selectedForeground) {
                        _txDropStaleFg.fetch_add(1, std::memory_order_relaxed);
                    }
                    else {
                        _txDropStaleBg.fetch_add(1, std::memory_order_relaxed);
                    }
                    recordStaleProbe(
                        nowUs,
                        StaleAction::HardDrop,
                        selected,
                        selectedForeground,
                        staleReason,
                        overdueUs,
                        0ull,
                        1.0f);
                    consumeSelectedStateSlot(selectedSlot, selected.seq);
                    continue;
                }

                const auto extraOverdueUs = overdueUs - static_cast<std::uint64_t>(cfg.hidStaleUs);
                float salvageGain = 1.0f;
                if (softWindowUs > 0ull) {
                    salvageGain = 1.0f -
                        (static_cast<float>(extraOverdueUs) / static_cast<float>(softWindowUs));
                    salvageGain = std::clamp(salvageGain, 0.30f, 1.0f);
                }

                selected.leftMotor = static_cast<std::uint8_t>(std::clamp(
                    static_cast<float>(selected.leftMotor) * salvageGain, 0.0f, 255.0f));
                selected.rightMotor = static_cast<std::uint8_t>(std::clamp(
                    static_cast<float>(selected.rightMotor) * salvageGain, 0.0f, 255.0f));

                if (selected.leftMotor == 0 && selected.rightMotor == 0) {
                    _txSoftSalvageDropped.fetch_add(1, std::memory_order_relaxed);
                    if (selectedForeground) {
                        _txDropStaleFg.fetch_add(1, std::memory_order_relaxed);
                    }
                    else {
                        _txDropStaleBg.fetch_add(1, std::memory_order_relaxed);
                    }
                    recordStaleProbe(
                        nowUs,
                        StaleAction::SalvageZeroDrop,
                        selected,
                        selectedForeground,
                        staleReason,
                        overdueUs,
                        softWindowUs,
                        salvageGain);
                    consumeSelectedStateSlot(selectedSlot, selected.seq);
                    continue;
                }

                if (selectedForeground) {
                    _txSoftSalvageFg.fetch_add(1, std::memory_order_relaxed);
                }
                else {
                    _txSoftSalvageBg.fetch_add(1, std::memory_order_relaxed);
                }
                recordStaleProbe(
                    nowUs,
                    StaleAction::SoftSalvage,
                    selected,
                    selectedForeground,
                    staleReason,
                    overdueUs,
                    softWindowUs,
                    salvageGain);
            }

            if (_lastSentValid) {
                const bool sameOutput =
                    _lastSentLeft == selected.leftMotor &&
                    _lastSentRight == selected.rightMotor &&
                    _lastSentEvent == selected.eventType;
                const auto deltaUs = (nowUs > _lastSendQpc) ? (nowUs - _lastSendQpc) : 0ull;
                const auto minRepeatUs = (selected.leftMotor == 0 && selected.rightMotor == 0) ?
                    static_cast<std::uint64_t>(cfg.hidIdleRepeatIntervalUs) :
                    static_cast<std::uint64_t>(cfg.hidMinRepeatIntervalUs);
                if (sameOutput && deltaUs < minRepeatUs) {
                    _txSkippedRepeat.fetch_add(1, std::memory_order_relaxed);
                    recordRepeatGuardProbe(nowUs, selected, selectedForeground, deltaUs, minRepeatUs);
                    const bool nonZero = (selected.leftMotor != 0 || selected.rightMotor != 0);
                    const bool structuredSignal =
                        (selected.eventType != EventType::Unknown || selected.foregroundHint);
                    const bool keepPending =
                        nonZero &&
                        structuredSignal;
                    const auto selectedTargetUs = (selected.qpcTarget != 0) ? selected.qpcTarget : selected.qpc;
                    const auto selectedOverUs = (selectedTargetUs != 0 && nowUs > selectedTargetUs) ?
                        (nowUs - selectedTargetUs) :
                        0ull;
                    const auto selectedAheadUs = (selectedTargetUs > nowUs) ? (selectedTargetUs - nowUs) : 0ull;
                    const auto selectedAgeUs = (selected.qpc != 0 && nowUs > selected.qpc) ?
                        (nowUs - selected.qpc) :
                        0ull;
                    if (ShouldEmitWindowedProbe(
                            s_repeatEvalProbeWindowUs,
                            s_repeatEvalProbeLines,
                            nowUs,
                            14)) {
                        logger::info(
                            "[Haptics][ProbeTxDetail] site=flush/repeat_guard_eval branch=same_output slot={} seq={} evt={} motor={}/{} keep={} structured={} nonZero={} delta={}us minReq={}us target={} ahead={}us over={}us age={}us lead={}us",
                            slotLabelById(selectedSlot),
                            selected.seq,
                            ToString(selected.eventType),
                            static_cast<int>(selected.leftMotor),
                            static_cast<int>(selected.rightMotor),
                            keepPending ? 1 : 0,
                            structuredSignal ? 1 : 0,
                            nonZero ? 1 : 0,
                            deltaUs,
                            minRepeatUs,
                            selectedTargetUs,
                            selectedAheadUs,
                            selectedOverUs,
                            selectedAgeUs,
                            (selected.qpcTarget >= selected.qpc) ? (selected.qpcTarget - selected.qpc) : 0ull);
                    }
                    if (keepPending) {
                        std::scoped_lock lock(_stateMutex);
                        if (auto* stateSlot = stateSlotById(selectedSlot);
                            stateSlot && stateSlot->valid && stateSlot->frame.seq == selected.seq) {
                            // Keep pending frame in place and wait for min repeat interval
                            // to elapse naturally; do not push qpcTarget forward repeatedly.
                            stateSlot->updatedUs = nowUs;
                            if (ShouldEmitWindowedProbe(
                                    s_repeatKeepProbeWindowUs,
                                    s_repeatKeepProbeLines,
                                    nowUs,
                                    10)) {
                                const auto curTargetUs = (stateSlot->frame.qpcTarget != 0) ?
                                    stateSlot->frame.qpcTarget :
                                    stateSlot->frame.qpc;
                                const auto curLeadUs = (stateSlot->frame.qpcTarget >= stateSlot->frame.qpc) ?
                                    (stateSlot->frame.qpcTarget - stateSlot->frame.qpc) :
                                    0ull;
                                logger::info(
                                    "[Haptics][ProbeTxDetail] site=flush/repeat_guard_keep branch=keep_pending slot={} seq={} evt={} motor={}/{} delta={}us minReq={}us target={} ahead={}us over={}us lead={}us",
                                    slotLabelById(selectedSlot),
                                    stateSlot->frame.seq,
                                    ToString(stateSlot->frame.eventType),
                                    static_cast<int>(stateSlot->frame.leftMotor),
                                    static_cast<int>(stateSlot->frame.rightMotor),
                                    deltaUs,
                                    minRepeatUs,
                                    curTargetUs,
                                    (curTargetUs > nowUs) ? (curTargetUs - nowUs) : 0ull,
                                    (nowUs > curTargetUs) ? (nowUs - curTargetUs) : 0ull,
                                    curLeadUs);
                            }
                        }
                        else if (ShouldEmitWindowedProbe(
                                     s_repeatDropProbeWindowUs,
                                     s_repeatDropProbeLines,
                                     nowUs,
                                     8)) {
                            logger::info(
                                "[Haptics][ProbeTxDetail] site=flush/repeat_guard_drop branch=stale_or_replaced slot={} seq={} delta={}us minReq={}us",
                                slotLabelById(selectedSlot),
                                selected.seq,
                                deltaUs,
                                minRepeatUs);
                        }
                    }
                    else {
                        if (ShouldEmitWindowedProbe(
                                s_repeatDropProbeWindowUs,
                                s_repeatDropProbeLines,
                                nowUs,
                                8)) {
                            logger::info(
                                "[Haptics][ProbeTxDetail] site=flush/repeat_guard_drop branch=consume_no_keep slot={} seq={} evt={} nonZero={} structured={} delta={}us minReq={}us",
                                slotLabelById(selectedSlot),
                                selected.seq,
                                ToString(selected.eventType),
                                nonZero ? 1 : 0,
                                structuredSignal ? 1 : 0,
                                deltaUs,
                                minRepeatUs);
                        }
                        consumeSelectedStateSlot(selectedSlot, selected.seq);
                    }
                    break;
                }
            }

            bool ok = false;
            {
                std::scoped_lock lock(_deviceMutex);
                _device = device;
                ok = SendVibrateCommandUnlocked(selected.leftMotor, selected.rightMotor);
            }

            if (ok) {
                _totalFramesSent.fetch_add(1, std::memory_order_relaxed);
                _totalBytesSent.fetch_add(48, std::memory_order_relaxed);
                _sendWriteOk.fetch_add(1, std::memory_order_relaxed);
                if (selectedForeground) {
                    _txDequeuedFg.fetch_add(1, std::memory_order_relaxed);
                }
                else {
                    _txDequeuedBg.fetch_add(1, std::memory_order_relaxed);
                }
                consumeSelectedStateSlot(selectedSlot, selected.seq);
            }
            else {
                _sendFailures.fetch_add(1, std::memory_order_relaxed);
                _txSendFail.fetch_add(1, std::memory_order_relaxed);
            }

            const auto sendUs = ToQPC(Now());
            const auto latencyUs = (selected.qpc != 0 && sendUs > selected.qpc) ?
                static_cast<std::uint32_t>(sendUs - selected.qpc) :
                0u;
            const auto expectedDeltaUs = (_lastTargetQpc != 0 && selected.qpcTarget >= _lastTargetQpc) ?
                static_cast<std::uint32_t>(selected.qpcTarget - _lastTargetQpc) :
                0u;
            const auto actualDeltaUs = (_lastSendQpc != 0 && sendUs >= _lastSendQpc) ?
                static_cast<std::uint32_t>(sendUs - _lastSendQpc) :
                0u;
            const auto jitterUs = (expectedDeltaUs > actualDeltaUs) ?
                (expectedDeltaUs - actualDeltaUs) :
                (actualDeltaUs - expectedDeltaUs);

            PushLatencySample(latencyUs, jitterUs);
            _lastSendQpc = sendUs;
            _lastTargetQpc = selected.qpcTarget;
            _lastSentLeft = selected.leftMotor;
            _lastSentRight = selected.rightMotor;
            _lastSentEvent = selected.eventType;
            _lastSentValid = true;
            sentThisFlush += 1;
        }

        if (sentThisFlush >= maxSendPerFlush) {
            const auto nowUs = ToQPC(Now());
            const auto snap = snapshotState(nowUs);
            if (snap.dueFg > 0 || snap.dueBg > 0) {
                _txFlushCapHit.fetch_add(1, std::memory_order_relaxed);
                _txFlushCapDueFg.fetch_add(snap.dueFg, std::memory_order_relaxed);
                _txFlushCapDueBg.fetch_add(snap.dueBg, std::memory_order_relaxed);
                recordCapHitProbe(nowUs, snap, sentThisFlush);
            }
        }
    }

    HidOutput::Stats HidOutput::GetStats() const
    {
        Stats s;
        s.totalFramesSent = _totalFramesSent.load(std::memory_order_relaxed);
        s.totalBytesSent = _totalBytesSent.load(std::memory_order_relaxed);
        s.sendFailures = _sendFailures.load(std::memory_order_relaxed);
        s.sendNoDevice = _sendNoDevice.load(std::memory_order_relaxed);
        s.sendWriteFail = _sendWriteFail.load(std::memory_order_relaxed);
        s.sendWriteOk = _sendWriteOk.load(std::memory_order_relaxed);

        s.txQueuedFg = _txQueuedFg.load(std::memory_order_relaxed);
        s.txQueuedBg = _txQueuedBg.load(std::memory_order_relaxed);
        s.txDequeuedFg = _txDequeuedFg.load(std::memory_order_relaxed);
        s.txDequeuedBg = _txDequeuedBg.load(std::memory_order_relaxed);
        s.txDropQueueFullFg = _txDropQueueFullFg.load(std::memory_order_relaxed);
        s.txDropQueueFullBg = _txDropQueueFullBg.load(std::memory_order_relaxed);
        s.txDropStaleFg = _txDropStaleFg.load(std::memory_order_relaxed);
        s.txDropStaleBg = _txDropStaleBg.load(std::memory_order_relaxed);
        s.txMergedFg = _txMergedFg.load(std::memory_order_relaxed);
        s.txMergedBg = _txMergedBg.load(std::memory_order_relaxed);
        s.txSendOk = _sendWriteOk.load(std::memory_order_relaxed);
        s.txSendFail = _txSendFail.load(std::memory_order_relaxed);
        s.txNoDevice = _sendNoDevice.load(std::memory_order_relaxed);
        s.txQueueDepthFg = _txQueueDepthFg.load(std::memory_order_relaxed);
        s.txQueueDepthBg = _txQueueDepthBg.load(std::memory_order_relaxed);

        std::vector<std::uint32_t> latencySamples;
        std::vector<std::uint32_t> jitterSamples;
        std::vector<std::uint32_t> trackRenderOverSamples;
        std::vector<std::uint32_t> trackFirstRenderSamples;
        {
            std::scoped_lock lock(_sampleMutex);
            latencySamples.assign(_latencySamplesUs.begin(), _latencySamplesUs.end());
            jitterSamples.assign(_jitterSamplesUs.begin(), _jitterSamplesUs.end());
            trackRenderOverSamples.assign(_trackRenderOverSamplesUs.begin(), _trackRenderOverSamplesUs.end());
            trackFirstRenderSamples.assign(_trackFirstRenderSamplesUs.begin(), _trackFirstRenderSamplesUs.end());
        }

        s.txLatencySamples = static_cast<std::uint32_t>(latencySamples.size());
        s.txLatencyP50Us = PercentileOf(latencySamples, 0.50f);
        s.txLatencyP95Us = PercentileOf(latencySamples, 0.95f);
        s.txJitterP95Us = PercentileOf(jitterSamples, 0.95f);
        s.txRenderOverSamples = static_cast<std::uint32_t>(trackRenderOverSamples.size());
        s.txRenderOverP50Us = PercentileOf(trackRenderOverSamples, 0.50f);
        s.txRenderOverP95Us = PercentileOf(trackRenderOverSamples, 0.95f);
        s.txFirstRenderSamples = static_cast<std::uint32_t>(trackFirstRenderSamples.size());
        s.txFirstRenderP50Us = PercentileOf(trackFirstRenderSamples, 0.50f);
        s.txFirstRenderP95Us = PercentileOf(trackFirstRenderSamples, 0.95f);
        s.txSkippedRepeat = _txSkippedRepeat.load(std::memory_order_relaxed);
        s.txStopFlushes = _txStopFlushes.load(std::memory_order_relaxed);
        s.txRouteFg = _txRouteFg.load(std::memory_order_relaxed);
        s.txRouteBg = _txRouteBg.load(std::memory_order_relaxed);
        s.txRouteFgZero = _txRouteFgZero.load(std::memory_order_relaxed);
        s.txRouteFgHint = _txRouteFgHint.load(std::memory_order_relaxed);
        s.txRouteFgPriority = _txRouteFgPriority.load(std::memory_order_relaxed);
        s.txRouteFgEvent = _txRouteFgEvent.load(std::memory_order_relaxed);
        s.txRouteBgUnknown = _txRouteBgUnknown.load(std::memory_order_relaxed);
        s.txRouteBgBackground = _txRouteBgBackground.load(std::memory_order_relaxed);
        s.txSelectForcedFgBudget = _txSelectForcedFgBudget.load(std::memory_order_relaxed);
        s.txSelectBgWhileFgPending = _txSelectBgWhileFgPending.load(std::memory_order_relaxed);
        s.txSoftSalvageFg = _txSoftSalvageFg.load(std::memory_order_relaxed);
        s.txSoftSalvageBg = _txSoftSalvageBg.load(std::memory_order_relaxed);
        s.txSoftSalvageDropped = _txSoftSalvageDropped.load(std::memory_order_relaxed);
        s.txFlushCapHit = _txFlushCapHit.load(std::memory_order_relaxed);
        s.txFlushCapDueFg = _txFlushCapDueFg.load(std::memory_order_relaxed);
        s.txFlushCapDueBg = _txFlushCapDueBg.load(std::memory_order_relaxed);
        s.txFlushNoSelectPending = _txFlushNoSelectPending.load(std::memory_order_relaxed);
        s.txFlushLookaheadMiss = _txFlushLookaheadMiss.load(std::memory_order_relaxed);
        s.txStateUpdateFg = _txStateUpdateFg.load(std::memory_order_relaxed);
        s.txStateUpdateBg = _txStateUpdateBg.load(std::memory_order_relaxed);
        s.txStateOverwriteFg = _txStateOverwriteFg.load(std::memory_order_relaxed);
        s.txStateOverwriteBg = _txStateOverwriteBg.load(std::memory_order_relaxed);
        s.txStateCarryQueuedFg = _txStateCarryQueuedFg.load(std::memory_order_relaxed);
        s.txStateCarryQueuedBg = _txStateCarryQueuedBg.load(std::memory_order_relaxed);
        s.txStateCarryDropFg = _txStateCarryDropFg.load(std::memory_order_relaxed);
        s.txStateCarryDropBg = _txStateCarryDropBg.load(std::memory_order_relaxed);
        s.txStateCarryConsumedFg = _txStateCarryConsumedFg.load(std::memory_order_relaxed);
        s.txStateCarryConsumedBg = _txStateCarryConsumedBg.load(std::memory_order_relaxed);
        s.txStateExpiredDrop = _txStateExpiredDrop.load(std::memory_order_relaxed);
        s.txStateFutureSkip = _txStateFutureSkip.load(std::memory_order_relaxed);
        return s;
    }

    bool HidOutput::SendVibrateCommand(std::uint8_t leftMotor, std::uint8_t rightMotor)
    {
        std::scoped_lock lock(_deviceMutex);
        return SendVibrateCommandUnlocked(leftMotor, rightMotor);
    }

    bool HidOutput::SendVibrateCommandUnlocked(std::uint8_t leftMotor, std::uint8_t rightMotor)
    {
        if (!_device) {
            static std::atomic_uint32_t s_noDevLog{ 0 };
            auto n = s_noDevLog.fetch_add(1);
            if (n < 5 || (n % 500) == 0) {
                logger::warn("[Haptics][HidOutput] send skipped: no device");
            }
            _sendNoDevice.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // DualSense 输出报文（当前工程使用的简化格式）
        unsigned char buf[48]{};
        buf[0] = 0x02;        // Report ID
        buf[1] = 0xFF;        // flags
        buf[2] = 0xF7;        // flags2
        buf[3] = rightMotor;  // right (small)
        buf[4] = leftMotor;   // left (large)

        const int written = hid_write(_device, buf, static_cast<size_t>(sizeof(buf)));
        if (written < 0) {
            const wchar_t* err = hid_error(_device);
            _sendWriteFail.fetch_add(1, std::memory_order_relaxed);
            logger::warn("[Haptics][HidOutput] hid_write failed: {}", WideToUtf8(err));
            return false;
        }

        return true;
    }
}

