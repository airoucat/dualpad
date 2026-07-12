#pragma once

#include "input_v2/ingress/InputResetReason.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace dualpad::input_v2::runtime
{
    enum class InputRecoveryFailure : std::uint8_t
    {
        None = 0,
        StaleInputStateEpoch,
        StaleGamepadSession
    };

    struct InputRecoveryObservation
    {
        ingress::InputResetMarker marker{};
        std::uint64_t lastAppliedInputStateEpoch{ 0 };
        std::uint64_t lastAppliedGamepadSessionId{ 0 };
    };

    struct InputRecoveryRequest
    {
        bool valid{ false };
        InputRecoveryFailure failure{ InputRecoveryFailure::None };
        ingress::InputResetReasonMask reasons{ 0 };
        ingress::InputResetScope scope{ ingress::InputResetScope::GlobalInputState };
        std::uint64_t previousInputStateEpoch{ 0 };
        std::uint64_t previousGamepadSessionId{ 0 };
        std::uint64_t nextInputStateEpoch{ 0 };
        std::uint64_t nextGamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        bool clearAllVirtualOutput{ false };
        bool clearGamepadOutput{ false };
        bool clearKeyboardMouseOutput{ false };
        bool quarantineKeyboardMouse{ false };
        bool resetSyntheticSuppression{ false };
    };

    [[nodiscard]] InputRecoveryRequest BuildInputRecoveryRequest(
        const InputRecoveryObservation& observation) noexcept;

    class InputRecoveryMailbox
    {
    public:
        static InputRecoveryMailbox& GetSingleton();

        bool Publish(InputRecoveryRequest request);
        [[nodiscard]] std::vector<InputRecoveryRequest> ConsumeAll();
        void Reset();
        void ResetForTests();

    private:
        static constexpr std::size_t kCapacity = 16;

        std::mutex _mutex;
        std::deque<InputRecoveryRequest> _requests;
    };
}
