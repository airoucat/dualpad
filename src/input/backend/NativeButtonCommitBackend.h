#pragma once

#include "input/backend/FrameActionPlan.h"
#include "input/backend/NativeControlCode.h"
#include "input/backend/PulseGenerationContract.h"

#ifndef DUALPAD_REPLAY_HARNESS
#include "input/backend/PollCommitCoordinator.h"
#endif

#include <cstdint>
#include <mutex>
#include <string_view>

namespace dualpad::input::backend
{
#ifdef DUALPAD_REPLAY_HARNESS
    enum class HeldContributor : std::uint8_t
    {
        None = 0,
        Gamepad = 1u << 0,
        KeyboardMouse = 1u << 1,
        MousePhysical = 1u << 2
    };

    enum class HeldEmitterSource : std::uint8_t
    {
        None = 0,
        Gamepad,
        KeyboardMouse
    };
#else
    enum class NativeButtonCommitTranslationKind : std::uint8_t
    {
        Request = 0,
        Noop,
        Invalid
    };

    struct NativeButtonCommitTranslation
    {
        NativeButtonCommitTranslationKind kind{ NativeButtonCommitTranslationKind::Invalid };
        PollCommitMode mode{ PollCommitMode::None };
        PollCommitRequestKind requestKind{ PollCommitRequestKind::None };
        HeldContributor contributor{ HeldContributor::None };
    };

    [[nodiscard]] inline NativeButtonCommitTranslation TranslatePlannedActionForNativeButtonCommit(
        const PlannedAction& action) noexcept
    {
        if (action.backend != PlannedBackend::NativeButtonCommit ||
            action.kind != PlannedActionKind::NativeButton ||
            action.digitalPolicy == NativeDigitalPolicyKind::None ||
            action.outputCode == 0) {
            return {};
        }

        switch (action.digitalPolicy) {
        case NativeDigitalPolicyKind::DeferredPulse:
        case NativeDigitalPolicyKind::PulseMinDown:
            if (action.phase == PlannedActionPhase::Pulse ||
                action.phase == PlannedActionPhase::Press) {
                return {
                    .kind = NativeButtonCommitTranslationKind::Request,
                    .mode = PollCommitMode::Pulse,
                    .requestKind = PollCommitRequestKind::Pulse
                };
            }
            if (action.phase == PlannedActionPhase::Release) {
                return { .kind = NativeButtonCommitTranslationKind::Noop };
            }
            return {};

        case NativeDigitalPolicyKind::HoldOwner:
            if (action.phase == PlannedActionPhase::Release) {
                return {
                    .kind = NativeButtonCommitTranslationKind::Request,
                    .mode = PollCommitMode::Hold,
                    .requestKind = PollCommitRequestKind::HoldClear,
                    .contributor = HeldContributor::Gamepad
                };
            }
            if (action.phase == PlannedActionPhase::Press ||
                action.phase == PlannedActionPhase::Hold) {
                return {
                    .kind = NativeButtonCommitTranslationKind::Request,
                    .mode = PollCommitMode::Hold,
                    .requestKind = PollCommitRequestKind::HoldSet,
                    .contributor = HeldContributor::Gamepad
                };
            }
            return {};

        case NativeDigitalPolicyKind::RepeatOwner:
            if (action.phase == PlannedActionPhase::Release) {
                return {
                    .kind = NativeButtonCommitTranslationKind::Request,
                    .mode = PollCommitMode::Repeat,
                    .requestKind = PollCommitRequestKind::RepeatClear,
                    .contributor = HeldContributor::Gamepad
                };
            }
            if (action.phase == PlannedActionPhase::Press ||
                action.phase == PlannedActionPhase::Hold) {
                return {
                    .kind = NativeButtonCommitTranslationKind::Request,
                    .mode = PollCommitMode::Repeat,
                    .requestKind = PollCommitRequestKind::RepeatSet,
                    .contributor = HeldContributor::Gamepad
                };
            }
            return {};

        case NativeDigitalPolicyKind::ToggleDebounced:
            if (action.phase == PlannedActionPhase::Pulse ||
                action.phase == PlannedActionPhase::Press) {
                return {
                    .kind = NativeButtonCommitTranslationKind::Request,
                    .mode = PollCommitMode::Toggle,
                    .requestKind = PollCommitRequestKind::ToggleFire
                };
            }
            return {};

        case NativeDigitalPolicyKind::None:
        default:
            return {};
        }
    }
#endif

    struct CommittedButtonState
    {
        std::uint32_t buttonDownMask{ 0 };
        std::uint32_t buttonPressedMask{ 0 };
        std::uint32_t buttonReleasedMask{ 0 };
        std::uint32_t managedMask{ 0 };
        InputContext context{ InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
        std::uint64_t pollSequence{ 0 };
        PulseGenerationRecord pulse{};
    };

    inline constexpr bool IsNativeDigitalGateOpenForContext(InputContext) noexcept
    {
        // Gameplay ownership suppression is applied before queueing actions.
        // The poll commit gate must not block menu or compatibility contexts.
        return true;
    }

    class NativeButtonCommitBackend final
#ifndef DUALPAD_REPLAY_HARNESS
        : public IPollCommitEmitter
#endif
    {
    public:
        static NativeButtonCommitBackend& GetSingleton();

        void Reset();
        bool IsRouteActive() const;
        bool CanHandleAction(std::string_view actionId) const;
        bool IsActionDown(std::string_view actionId) const;
        bool HasHeldContributor(std::string_view actionId, HeldContributor contributor) const;
        HeldEmitterSource GetHeldEmitter(std::string_view actionId) const;
        bool SyncHeldContributors(
            std::string_view actionId,
            NativeControlCode outputCode,
            std::uint8_t activeSourceMask,
            bool virtualBridgeDesired,
            InputContext context,
            std::uint32_t contextEpoch);

        void BeginFrame(
            InputContext context,
            std::uint32_t contextEpoch,
            std::uint64_t nowUs,
            std::uint64_t runtimeGeneration);

        void SetGameplayDigitalGatePlan(bool suppressNewTransientActions);
        bool ApplyPlannedAction(const PlannedAction& action);
        void ForceCancelGateAwareGameplayTransientActions();
        void CancelForBoundary(PulseBoundaryReason reason, std::uint64_t runtimeGeneration);
        [[nodiscard]] CommittedButtonState CommitPollState(std::uint64_t runtimeGeneration);

#ifndef DUALPAD_REPLAY_HARNESS
        EmitResult Emit(const EmitRequest& request) override;
#endif

    private:
#ifdef DUALPAD_REPLAY_HARNESS
        NativeButtonCommitBackend() = default;
#else
        struct SprintProbeSnapshot
        {
            bool valid{ false };
            bool kbmHeld{ false };
            bool gamepadContributor{ false };
            bool keyboardMouseContributor{ false };
            bool effectiveHeld{ false };
            bool actionDown{ false };
            bool managed{ false };
            bool gameplayOwnerGamepad{ false };
            ExecState state{ ExecState::Idle };
            HeldEmitterSource activeEmitter{ HeldEmitterSource::None };
            InputContext context{ InputContext::Gameplay };
            std::uint32_t contextEpoch{ 0 };
        };

        struct SneakProbeSnapshot
        {
            bool valid{ false };
            bool actionDown{ false };
            bool managed{ false };
            bool gateAware{ false };
            ExecState state{ ExecState::Idle };
            PollCommitMode mode{ PollCommitMode::None };
            InputContext context{ InputContext::Gameplay };
            std::uint32_t contextEpoch{ 0 };
            std::uint64_t pollSequence{ 0 };
        };

        NativeButtonCommitBackend() = default;

        static NativeButtonCommitTranslationKind TranslatePlannedActionToCommitRequest(
            const PlannedAction& action,
            PollCommitRequest& outRequest);

        static std::uint64_t NowUs();
        static std::uint32_t ToVirtualPadBit(NativeControlCode code);
        static bool ShouldLogPollCommit();
        static bool IsGameplayGateOpen(InputContext context);
        static bool SlotIsDown(const PollCommitSlot& slot);
        static bool SlotIsManaged(const PollCommitSlot& slot);
        PollCommitCoordinator _pollCommit{};
        InputContext _frameContext{ InputContext::Gameplay };
        std::uint32_t _frameContextEpoch{ 0 };
        std::uint64_t _pollSequence{ 0 };
        std::uint32_t _lastCommittedButtonDownMask{ 0 };
        SprintProbeSnapshot _lastSprintProbeSnapshot{};
        SneakProbeSnapshot _lastSneakProbeSnapshot{};
        bool _suppressGameplayDigitalTransientActions{ false };
        mutable std::mutex _lock;
#endif
    };
}
