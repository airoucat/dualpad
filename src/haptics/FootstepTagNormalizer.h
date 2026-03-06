#pragma once

#include <cstdint>
#include <string_view>

namespace dualpad::haptics
{
    enum class FootstepTruthSide : std::uint8_t
    {
        Unknown = 0,
        Left,
        Right
    };

    enum class FootstepTruthGait : std::uint8_t
    {
        Unknown = 0,
        Walk,
        Sprint,
        JumpUp,
        JumpDown
    };

    enum class FootstepTexturePreset : std::uint8_t
    {
        None = 0,
        WalkSoft,
        SprintDrive,
        JumpLift,
        LandSlam
    };

    struct NormalizedFootstepTag
    {
        std::string_view rawTag{};
        std::string_view canonicalTag{};
        FootstepTruthSide side{ FootstepTruthSide::Unknown };
        FootstepTruthGait gait{ FootstepTruthGait::Unknown };
        bool valid{ false };
    };

    inline constexpr const char* ToString(FootstepTruthGait gait)
    {
        switch (gait) {
        case FootstepTruthGait::Walk:
            return "Walk";
        case FootstepTruthGait::Sprint:
            return "Sprint";
        case FootstepTruthGait::JumpUp:
            return "JumpUp";
        case FootstepTruthGait::JumpDown:
            return "JumpDown";
        default:
            return "Unknown";
        }
    }

    inline constexpr const char* ToString(FootstepTexturePreset preset)
    {
        switch (preset) {
        case FootstepTexturePreset::WalkSoft:
            return "WalkSoft";
        case FootstepTexturePreset::SprintDrive:
            return "SprintDrive";
        case FootstepTexturePreset::JumpLift:
            return "JumpLift";
        case FootstepTexturePreset::LandSlam:
            return "LandSlam";
        default:
            return "None";
        }
    }

    inline constexpr bool IsStrideGait(FootstepTruthGait gait)
    {
        return gait == FootstepTruthGait::Walk || gait == FootstepTruthGait::Sprint;
    }

    inline constexpr bool SameStrideFamily(FootstepTruthGait lhs, FootstepTruthGait rhs)
    {
        return IsStrideGait(lhs) && IsStrideGait(rhs);
    }

    inline constexpr NormalizedFootstepTag NormalizeFootstepTruthTag(std::string_view rawTag)
    {
        if (rawTag.empty()) {
            return {};
        }

        if (rawTag.find("FootSprintLeft") != std::string_view::npos) {
            return {
                .rawTag = rawTag,
                .canonicalTag = "StrideLeft",
                .side = FootstepTruthSide::Left,
                .gait = FootstepTruthGait::Sprint,
                .valid = true
            };
        }
        if (rawTag.find("FootSprintRight") != std::string_view::npos) {
            return {
                .rawTag = rawTag,
                .canonicalTag = "StrideRight",
                .side = FootstepTruthSide::Right,
                .gait = FootstepTruthGait::Sprint,
                .valid = true
            };
        }
        if (rawTag.find("FootLeft") != std::string_view::npos) {
            return {
                .rawTag = rawTag,
                .canonicalTag = "StrideLeft",
                .side = FootstepTruthSide::Left,
                .gait = FootstepTruthGait::Walk,
                .valid = true
            };
        }
        if (rawTag.find("FootRight") != std::string_view::npos) {
            return {
                .rawTag = rawTag,
                .canonicalTag = "StrideRight",
                .side = FootstepTruthSide::Right,
                .gait = FootstepTruthGait::Walk,
                .valid = true
            };
        }
        if (rawTag.find("JumpUp") != std::string_view::npos) {
            return {
                .rawTag = rawTag,
                .canonicalTag = "JumpUp",
                .side = FootstepTruthSide::Unknown,
                .gait = FootstepTruthGait::JumpUp,
                .valid = true
            };
        }
        if (rawTag.find("JumpDown") != std::string_view::npos) {
            return {
                .rawTag = rawTag,
                .canonicalTag = "JumpDown",
                .side = FootstepTruthSide::Unknown,
                .gait = FootstepTruthGait::JumpDown,
                .valid = true
            };
        }

        if (rawTag.find("Left") != std::string_view::npos) {
            return {
                .rawTag = rawTag,
                .canonicalTag = "StrideLeft",
                .side = FootstepTruthSide::Left,
                .gait = FootstepTruthGait::Walk,
                .valid = true
            };
        }
        if (rawTag.find("Right") != std::string_view::npos) {
            return {
                .rawTag = rawTag,
                .canonicalTag = "StrideRight",
                .side = FootstepTruthSide::Right,
                .gait = FootstepTruthGait::Walk,
                .valid = true
            };
        }

        return {
            .rawTag = rawTag,
            .canonicalTag = rawTag,
            .side = FootstepTruthSide::Unknown,
            .gait = FootstepTruthGait::Unknown,
            .valid = false
        };
    }
}
