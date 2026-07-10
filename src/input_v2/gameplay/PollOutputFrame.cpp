#include "pch.h"

#include "input_v2/gameplay/PollOutputFrame.h"

#include "input_v2/runtime/RuntimeOwnerGuard.h"

#include <algorithm>

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        std::shared_ptr<const PollOutputFrame> NeutralFrame(PollOutputRouteHealth health)
        {
            auto frame = std::make_shared<PollOutputFrame>();
            frame->routeHealth = health;
            frame->neutral = true;
            return frame;
        }
    }

    PollOutputPublication::PollOutputPublication() :
        _initializing(NeutralFrame(PollOutputRouteHealth::Initializing)),
        _ownerDegraded(NeutralFrame(PollOutputRouteHealth::OwnerDegraded)),
        _unavailable(NeutralFrame(PollOutputRouteHealth::PublicationUnavailable)),
        _shutdown(NeutralFrame(PollOutputRouteHealth::Shutdown))
    {
        _published.store(_initializing, std::memory_order_release);
    }

    PollOutputPublication& PollOutputPublication::GetSingleton()
    {
        static PollOutputPublication publication;
        return publication;
    }

    std::shared_ptr<const PollOutputFrame> PollOutputPublication::AcquireForPoll() const noexcept
    {
        if (_forcedUnavailable.load(std::memory_order_acquire)) {
            return _unavailable;
        }
        const auto ownerFailure = runtime::RuntimeOwnerGuard::GetSingleton().GetPublishedFailure();
        if (ownerFailure == runtime::RuntimeOwnerFailure::OwnerStopped) {
            return _shutdown;
        }
        if (ownerFailure != runtime::RuntimeOwnerFailure::None) {
            return _ownerDegraded;
        }
        const auto published = _published.load(std::memory_order_acquire);
        return published ? published : _unavailable;
    }

    bool PollOutputPublication::PublishOwnerFrame(PollOutputFrame frame)
    {
        if (!runtime::RuntimeOwnerGuard::GetSingleton().IsCurrentThreadOwnerTick()) {
            return false;
        }
        std::scoped_lock lock(_writerMutex);
        PublishLocked(std::move(frame));
        return true;
    }

    void PollOutputPublication::PublishForTests(PollOutputFrame frame)
    {
        std::scoped_lock lock(_writerMutex);
        PublishLocked(std::move(frame));
    }

    void PollOutputPublication::PublishLocked(PollOutputFrame frame)
    {
        const auto previous = _published.load(std::memory_order_acquire);
        frame.publicationGeneration = ++_nextPublicationGeneration;
        if (!previous || !SameGamepadPayload(*previous, frame)) {
            ++_packetNumber;
        }
        frame.packetNumber = _packetNumber;
        _published.store(
            std::make_shared<const PollOutputFrame>(std::move(frame)),
            std::memory_order_release);
    }

    bool PollOutputPublication::SameGamepadPayload(
        const PollOutputFrame& lhs,
        const PollOutputFrame& rhs) noexcept
    {
        return lhs.buttons == rhs.buttons &&
            lhs.lx == rhs.lx &&
            lhs.ly == rhs.ly &&
            lhs.rx == rhs.rx &&
            lhs.ry == rhs.ry &&
            lhs.lt == rhs.lt &&
            lhs.rt == rhs.rt;
    }

    void PollOutputPublication::SetUnavailableForTests(bool unavailable) noexcept
    {
        _forcedUnavailable.store(unavailable, std::memory_order_release);
    }

    void PollOutputPublication::ResetForTests()
    {
        std::scoped_lock lock(_writerMutex);
        _nextPublicationGeneration = 0;
        _packetNumber = 0;
        _forcedUnavailable.store(false, std::memory_order_release);
        _published.store(_initializing, std::memory_order_release);
    }

    const char* ToString(PollOutputRouteHealth health) noexcept
    {
        switch (health) {
        case PollOutputRouteHealth::Ready:
            return "ready";
        case PollOutputRouteHealth::OwnerDegraded:
            return "owner_degraded";
        case PollOutputRouteHealth::PublicationUnavailable:
            return "publication_unavailable";
        case PollOutputRouteHealth::Shutdown:
            return "shutdown";
        case PollOutputRouteHealth::Initializing:
        default:
            return "initializing";
        }
    }

    std::int16_t EncodePollStickAxis(float value) noexcept
    {
        const auto clamped = std::clamp(value, -1.0f, 1.0f);
        return static_cast<std::int16_t>(clamped * 32767.0f);
    }

    std::uint8_t EncodePollTrigger(float value) noexcept
    {
        const auto clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<std::uint8_t>(clamped * 255.0f);
    }
}
