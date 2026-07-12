#include "pch.h"
#include "input/backend/NativeButtonCommitBackend.h"

namespace dualpad::input::backend
{
    NativeButtonCommitBackend& NativeButtonCommitBackend::GetSingleton()
    {
        static NativeButtonCommitBackend instance;
        return instance;
    }

    void NativeButtonCommitBackend::Reset()
    {
    }

    bool NativeButtonCommitBackend::IsRouteActive() const
    {
        return false;
    }

    bool NativeButtonCommitBackend::CanHandleAction(std::string_view) const
    {
        return false;
    }

    bool NativeButtonCommitBackend::IsActionDown(std::string_view) const
    {
        return false;
    }

    bool NativeButtonCommitBackend::HasHeldContributor(std::string_view, HeldContributor) const
    {
        return false;
    }

    HeldEmitterSource NativeButtonCommitBackend::GetHeldEmitter(std::string_view) const
    {
        return HeldEmitterSource::None;
    }

    bool NativeButtonCommitBackend::SyncHeldContributors(
        std::string_view,
        NativeControlCode,
        std::uint8_t,
        bool,
        InputContext,
        std::uint32_t)
    {
        return true;
    }

    void NativeButtonCommitBackend::BeginFrame(InputContext, std::uint32_t, std::uint64_t, std::uint64_t)
    {
    }

    void NativeButtonCommitBackend::SetGameplayDigitalGatePlan(bool)
    {
    }

    bool NativeButtonCommitBackend::ApplyPlannedAction(const PlannedAction&)
    {
        return true;
    }

    void NativeButtonCommitBackend::ForceCancelGateAwareGameplayTransientActions()
    {
    }

    void NativeButtonCommitBackend::CancelForBoundary(PulseBoundaryReason, std::uint64_t)
    {
    }

    CommittedButtonState NativeButtonCommitBackend::CommitPollState(std::uint64_t)
    {
        return {};
    }

}
