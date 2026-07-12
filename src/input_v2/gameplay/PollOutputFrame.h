#pragma once

#include "input_v2/compat/LegacyInputContextCompat.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace dualpad::input_v2::gameplay
{
    enum class PollOutputRouteHealth : std::uint8_t
    {
        Initializing = 0,
        Ready,
        OwnerDegraded,
        PublicationUnavailable,
        Shutdown
    };

    struct PollOutputFrame
    {
        std::uint64_t publicationGeneration{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
        std::uint64_t manifestEpoch{ 0 };
        std::uint64_t contextRevision{ 0 };
        std::uint64_t presentationEpoch{ 0 };
        std::uint64_t actionEpoch{ 0 };
        dualpad::input::InputContext context{ dualpad::input::InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
        std::uint32_t packetNumber{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        std::uint64_t orderedCutoffSeq{ 0 };
        std::uint64_t eventBatchToken{ 0 };

        std::uint16_t buttons{ 0 };
        std::uint32_t pressedMask{ 0 };
        std::uint32_t releasedMask{ 0 };
        std::int16_t lx{ 0 };
        std::int16_t ly{ 0 };
        std::int16_t rx{ 0 };
        std::int16_t ry{ 0 };
        std::uint8_t lt{ 0 };
        std::uint8_t rt{ 0 };
        std::uint64_t pulseToken{ 0 };
        std::uint64_t pulseDownGeneration{ 0 };
        std::uint64_t pulseUpGeneration{ 0 };

        PollOutputRouteHealth routeHealth{ PollOutputRouteHealth::Initializing };
        bool neutral{ true };
        // Owner-observed I-0 telemetry only; never participates in routing or payload identity.
        bool remapMode{ false };
        bool connected{ false };
        bool delegateReady{ false };
    };

    class PollOutputPublication
    {
    public:
        static PollOutputPublication& GetSingleton();

        [[nodiscard]] std::shared_ptr<const PollOutputFrame> AcquireForPoll() const noexcept;
        bool PublishOwnerFrame(PollOutputFrame frame);
        void PublishForTests(PollOutputFrame frame);
        void SetUnavailableForTests(bool unavailable) noexcept;
        void ResetForTests();

    private:
        PollOutputPublication();
        void PublishLocked(PollOutputFrame frame);
        static bool SameGamepadPayload(const PollOutputFrame& lhs, const PollOutputFrame& rhs) noexcept;

        mutable std::mutex _writerMutex;
        std::atomic<std::shared_ptr<const PollOutputFrame>> _published;
        std::shared_ptr<const PollOutputFrame> _initializing;
        std::shared_ptr<const PollOutputFrame> _ownerDegraded;
        std::shared_ptr<const PollOutputFrame> _unavailable;
        std::shared_ptr<const PollOutputFrame> _shutdown;
        std::uint64_t _nextPublicationGeneration{ 0 };
        std::uint32_t _packetNumber{ 0 };
        std::atomic_bool _forcedUnavailable{ false };
    };

    const char* ToString(PollOutputRouteHealth health) noexcept;
    std::int16_t EncodePollStickAxis(float value) noexcept;
    std::uint8_t EncodePollTrigger(float value) noexcept;
}
