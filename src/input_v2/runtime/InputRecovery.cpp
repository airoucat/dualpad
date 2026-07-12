#include "pch.h"

#include "input_v2/runtime/InputRecovery.h"

#include <utility>

namespace dualpad::input_v2::runtime
{
    InputRecoveryRequest BuildInputRecoveryRequest(
        const InputRecoveryObservation& observation) noexcept
    {
        const auto& marker = observation.marker;
        InputRecoveryRequest request{
            .reasons = marker.reasons,
            .scope = marker.scope,
            .previousInputStateEpoch = observation.lastAppliedInputStateEpoch,
            .previousGamepadSessionId = observation.lastAppliedGamepadSessionId,
            .nextInputStateEpoch = marker.inputStateEpoch,
            .nextGamepadSessionId = marker.gamepadSessionId,
            .contextRevision = marker.contextRevision,
            .controlMapRevision = marker.controlMapRevision
        };

        if (marker.inputStateEpoch < observation.lastAppliedInputStateEpoch ||
            (marker.scope == ingress::InputResetScope::GlobalInputState &&
                marker.inputStateEpoch == observation.lastAppliedInputStateEpoch)) {
            request.failure = InputRecoveryFailure::StaleInputStateEpoch;
            return request;
        }
        if (marker.gamepadSessionId < observation.lastAppliedGamepadSessionId ||
            (marker.scope == ingress::InputResetScope::GamepadSource &&
                marker.gamepadSessionId == observation.lastAppliedGamepadSessionId)) {
            request.failure = InputRecoveryFailure::StaleGamepadSession;
            return request;
        }

        request.valid = true;
        switch (marker.scope) {
        case ingress::InputResetScope::GlobalInputState:
            request.clearAllVirtualOutput = true;
            request.clearGamepadOutput = true;
            request.clearKeyboardMouseOutput = true;
            request.quarantineKeyboardMouse = true;
            request.resetSyntheticSuppression = true;
            break;
        case ingress::InputResetScope::KeyboardMouseSource:
            request.clearKeyboardMouseOutput = true;
            request.quarantineKeyboardMouse = true;
            request.resetSyntheticSuppression = true;
            break;
        case ingress::InputResetScope::GamepadSource:
            request.clearGamepadOutput = true;
            break;
        }
        return request;
    }

    InputRecoveryMailbox& InputRecoveryMailbox::GetSingleton()
    {
        static InputRecoveryMailbox mailbox;
        return mailbox;
    }

    bool InputRecoveryMailbox::Publish(InputRecoveryRequest request)
    {
        if (!request.valid) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        if (_requests.size() == kCapacity) {
            _requests.pop_front();
        }
        _requests.push_back(std::move(request));
        return true;
    }

    std::vector<InputRecoveryRequest> InputRecoveryMailbox::ConsumeAll()
    {
        std::scoped_lock lock(_mutex);
        std::vector<InputRecoveryRequest> consumed;
        consumed.reserve(_requests.size());
        while (!_requests.empty()) {
            consumed.push_back(std::move(_requests.front()));
            _requests.pop_front();
        }
        return consumed;
    }

    void InputRecoveryMailbox::Reset()
    {
        std::scoped_lock lock(_mutex);
        _requests.clear();
    }

    void InputRecoveryMailbox::ResetForTests()
    {
        Reset();
    }
}
