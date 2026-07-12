#pragma once

#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/KbmGameplayFacts.h"

#include <RE/Skyrim.h>

#include <cstdint>

namespace dualpad::input
{
    class SkyrimKbmInputAdapter
    {
    public:
        input_v2::ingress::KbmBindingSnapshot CaptureBindingSnapshot(
            const input_v2::context::ResolvedContextSnapshot& context);

        input_v2::ingress::KbmObservedBatch ObserveEventList(
            RE::InputEvent* const* events,
            const input_v2::ingress::KbmBindingSnapshot& bindings,
            std::uint64_t ownerTickToken,
            std::uint64_t eventBatchToken,
            std::uint64_t ownerNowUs) const;

    private:
        std::uint64_t _lastFingerprint{ 0 };
        std::uint64_t _bindingGeneration{ 0 };
    };
}
