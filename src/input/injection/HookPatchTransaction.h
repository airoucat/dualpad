#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dualpad::input::patching
{
    using Bytes = std::vector<std::uint8_t>;

    enum class HookOperationalState : std::uint8_t
    {
        Disabled = 0,
        SafePassthrough,
        Installed,
        UnsafePartial
    };

    enum class HookFailureDisposition : std::uint8_t
    {
        None = 0,
        NotRequired,
        RolledBack,
        FailClosed
    };

    constexpr const char* ToString(HookOperationalState state) noexcept
    {
        switch (state) {
        case HookOperationalState::Disabled:
            return "disabled";
        case HookOperationalState::SafePassthrough:
            return "safe_passthrough";
        case HookOperationalState::Installed:
            return "installed";
        case HookOperationalState::UnsafePartial:
            return "unsafe_partial";
        default:
            return "unknown";
        }
    }

    constexpr const char* ToString(HookFailureDisposition disposition) noexcept
    {
        switch (disposition) {
        case HookFailureDisposition::None:
            return "none";
        case HookFailureDisposition::NotRequired:
            return "not_required";
        case HookFailureDisposition::RolledBack:
            return "rolled_back";
        case HookFailureDisposition::FailClosed:
            return "fail_closed";
        default:
            return "unknown";
        }
    }

    enum class RelativePatchOpcode : std::uint8_t
    {
        Call = 0xE8,
        Jump = 0xE9
    };

    inline Bytes MakeRelativePatch(
        std::uintptr_t source,
        std::uintptr_t destination,
        RelativePatchOpcode opcode,
        std::size_t patchSize)
    {
        if (source == 0 || destination == 0 || patchSize < 5) {
            return {};
        }
        const auto displacement =
            static_cast<std::int64_t>(destination) -
            static_cast<std::int64_t>(source + 5);
        if (displacement < std::numeric_limits<std::int32_t>::min() ||
            displacement > std::numeric_limits<std::int32_t>::max()) {
            return {};
        }

        Bytes patch(patchSize, 0x90);
        patch[0] = static_cast<std::uint8_t>(opcode);
        const auto rel32 = static_cast<std::int32_t>(displacement);
        std::memcpy(patch.data() + 1, &rel32, sizeof(rel32));
        return patch;
    }

    inline std::optional<std::uintptr_t> DecodeRelativeTarget(
        std::uintptr_t source,
        const Bytes& bytes,
        RelativePatchOpcode opcode)
    {
        if (source == 0 || bytes.size() < 5 ||
            bytes[0] != static_cast<std::uint8_t>(opcode)) {
            return std::nullopt;
        }
        std::int32_t displacement = 0;
        std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
        return static_cast<std::uintptr_t>(
            static_cast<std::int64_t>(source + 5) + displacement);
    }

    inline Bytes MakeAbsoluteJump(std::uintptr_t destination)
    {
        if (destination == 0) {
            return {};
        }
        Bytes jump(14, 0);
        jump[0] = 0xFF;
        jump[1] = 0x25;
        const auto address = static_cast<std::uint64_t>(destination);
        std::memcpy(jump.data() + 6, &address, sizeof(address));
        return jump;
    }

    enum class PatchTransactionOutcome : std::uint8_t
    {
        Installed = 0,
        FailedNoWrite,
        RolledBack,
        UnsafePartial
    };

    struct PatchSite
    {
        std::string name;
        Bytes original;
        Bytes replacement;
        std::function<Bytes()> read;
        std::function<bool(const Bytes& expected, const Bytes& desired)> compareWrite;
    };

    struct PatchTransactionResult
    {
        PatchTransactionOutcome outcome{ PatchTransactionOutcome::FailedNoWrite };
        std::string failedSite;
        std::size_t appliedSites{ 0 };
        std::size_t rolledBackSites{ 0 };
    };

    namespace detail
    {
        inline std::optional<Bytes> ReadSite(const PatchSite& site) noexcept
        {
            try {
                if (!site.read) {
                    return std::nullopt;
                }
                return site.read();
            } catch (...) {
                return std::nullopt;
            }
        }

        inline bool CompareWrite(
            const PatchSite& site,
            const Bytes& expected,
            const Bytes& desired) noexcept
        {
            try {
                return site.compareWrite && site.compareWrite(expected, desired);
            } catch (...) {
                return false;
            }
        }
    }

    inline PatchTransactionResult ExecutePatchTransaction(std::vector<PatchSite>& sites)
    {
        PatchTransactionResult result{};
        std::vector<std::size_t> applied;
        applied.reserve(sites.size());

        for (const auto& site : sites) {
            const auto current = detail::ReadSite(site);
            if (site.name.empty() || site.original.empty() ||
                site.original.size() != site.replacement.size() ||
                !current || *current != site.original) {
                result.failedSite = site.name;
                return result;
            }
        }

        bool unsafeCurrentSite = false;
        bool applicationFailed = false;
        for (std::size_t index = 0; index < sites.size(); ++index) {
            auto& site = sites[index];
            result.failedSite = site.name;
            const auto writeReported = detail::CompareWrite(site, site.original, site.replacement);
            const auto current = detail::ReadSite(site);
            if (current && *current == site.replacement) {
                applied.push_back(index);
                result.appliedSites = applied.size();
                if (writeReported) {
                    continue;
                }
            } else if (!current || *current != site.original) {
                unsafeCurrentSite = true;
            }

            applicationFailed = true;
            break;
        }

        if (!applicationFailed && applied.size() == sites.size()) {
            result.outcome = PatchTransactionOutcome::Installed;
            result.failedSite.clear();
            return result;
        }

        bool rollbackFailed = unsafeCurrentSite;
        for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
            const auto& site = sites[*it];
            const auto restored = detail::CompareWrite(site, site.replacement, site.original);
            const auto current = detail::ReadSite(site);
            if (!restored || !current || *current != site.original) {
                rollbackFailed = true;
                continue;
            }
            ++result.rolledBackSites;
        }

        if (rollbackFailed) {
            result.outcome = PatchTransactionOutcome::UnsafePartial;
        } else if (applied.empty()) {
            result.outcome = PatchTransactionOutcome::FailedNoWrite;
        } else {
            result.outcome = PatchTransactionOutcome::RolledBack;
        }
        return result;
    }

    constexpr const char* ToString(PatchTransactionOutcome outcome) noexcept
    {
        switch (outcome) {
        case PatchTransactionOutcome::Installed:
            return "installed";
        case PatchTransactionOutcome::FailedNoWrite:
            return "failed_no_write";
        case PatchTransactionOutcome::RolledBack:
            return "rolled_back";
        case PatchTransactionOutcome::UnsafePartial:
            return "unsafe_partial";
        default:
            return "unknown";
        }
    }
}
