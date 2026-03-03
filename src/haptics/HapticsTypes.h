#pragma once
#include <cstdint>
#include <chrono>
#include <string_view>

namespace dualpad::haptics
{
    using TimePoint = std::chrono::steady_clock::time_point;

    inline TimePoint Now() { return std::chrono::steady_clock::now(); }

    inline std::uint64_t ToQPC(TimePoint tp)
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count());
    }

    inline TimePoint FromQPC(std::uint64_t us)
    {
        return TimePoint{} + std::chrono::microseconds(us);
    }

    enum class SemanticGroup : std::uint8_t
    {
        Unknown = 0, WeaponSwing, Hit, Block, Footstep, Bow, Voice, UI, Music, Ambient
    };

    enum class EventType : std::uint8_t
    {
        Unknown = 0,
        Footstep, WeaponSwing, HitImpact, SpellCast, SpellImpact, BowRelease, Jump, Land, Block, Shout,
        CustomEventStart = 128, CustomEventEnd = 255
    };

    inline constexpr std::string_view ToString(EventType type)
    {
        switch (type) {
        case EventType::Footstep:    return "Footstep";
        case EventType::WeaponSwing: return "WeaponSwing";
        case EventType::HitImpact:   return "HitImpact";
        case EventType::SpellCast:   return "SpellCast";
        case EventType::SpellImpact: return "SpellImpact";
        case EventType::BowRelease:  return "BowRelease";
        case EventType::Jump:        return "Jump";
        case EventType::Land:        return "Land";
        case EventType::Block:       return "Block";
        case EventType::Shout:       return "Shout";
        default:                     return "Unknown";
        }
    }

    struct DuckingRule
    {
        EventType focusType{ EventType::Unknown };
        EventType targetType{ EventType::Unknown };
        float duckFactor{ 1.0f };
    };

    struct EventMsg
    {
        std::uint64_t qpc{ 0 };
        EventType type{ EventType::Unknown };
        float intensity{ 1.0f };
        std::uint32_t actorId{ 0 };
        std::uint32_t formId{ 0 };
        SemanticGroup semanticHint{ SemanticGroup::Unknown };
    };

    struct AudioFeatureMsg
    {
        std::uint64_t qpcStart{ 0 };
        std::uint64_t qpcEnd{ 0 };
        std::uint32_t voiceId{ 0 };
        std::uint32_t sampleRate{ 48000 };
        std::uint16_t channels{ 2 };
        float rms{ 0.0f };
        float peak{ 0.0f };
        float bandLow{ 0.0f };
        float bandMid{ 0.0f };
        float bandHigh{ 0.0f };
        float attack{ 0.0f };
        float energyL{ 0.0f };
        float energyR{ 0.0f };
    };

    struct VoiceKey
    {
        std::uintptr_t voicePtr{ 0 };
        std::uint32_t generation{ 0 };
        bool operator==(const VoiceKey& o) const { return voicePtr == o.voicePtr && generation == o.generation; }
    };

    struct EventToken
    {
        std::uint64_t eventId{ 0 };
        std::uint64_t tEventUs{ 0 };
        EventType eventType{ EventType::Unknown };
        SemanticGroup semantic{ SemanticGroup::Unknown };
        float intensityHint{ 1.0f };
        std::uint32_t formId{ 0 };
        std::uint32_t actorId{ 0 };
    };

    struct AudioChunkFeature
    {
        std::uint64_t tSubmitUs{ 0 };
        VoiceKey voice{};
        std::uint32_t sampleRate{ 48000 };
        std::uint16_t channels{ 2 };
        float rms{ 0.0f };
        float peak{ 0.0f };
        float centroid{ 0.0f };
        float zcr{ 0.0f };
        float energyL{ 0.0f };
        float energyR{ 0.0f };
        std::uint32_t durationUs{ 0 };
    };

    enum class SourceType : std::uint8_t { BaseEvent, AudioMod, Ambient };

    struct HapticSourceMsg
    {
        std::uint64_t qpc{ 0 };
        SourceType type{ SourceType::BaseEvent };
        EventType eventType{ EventType::Unknown };
        float left{ 0.0f };
        float right{ 0.0f };
        float confidence{ 1.0f };
        int priority{ 50 };
        std::uint32_t ttlMs{ 100 };
    };

    struct HidFrame
    {
        std::uint64_t qpc{ 0 };
        std::uint8_t leftMotor{ 0 };
        std::uint8_t rightMotor{ 0 };
    };
}