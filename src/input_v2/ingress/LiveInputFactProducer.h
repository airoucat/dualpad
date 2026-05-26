#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input_v2/actions/LegacyInteractionInputAdapter.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <array>
#include <cstdint>
#include <vector>

namespace dualpad::input_v2::ingress
{
    class LiveInputFactProducer
    {
    public:
        static LiveInputFactProducer& GetSingleton();

        std::vector<actions::ControlSample> BuildControlSamples(
            const dualpad::input::PadEventSnapshot& snapshot,
            bool synthesizeDigitalEdges);
        void PublishGamepadSourceEvidence(
            const context::ResolvedContextSnapshot& contextSnapshot,
            std::uint64_t tick);
        void Reset();
        void ResetForTests();

    private:
        std::uint32_t _previousDownMask{ 0 };
        std::array<std::uint64_t, 32> _downAtUs{};
        presentation::DeviceFamilyIngressPublisher _deviceFamilyPublisher{};
        presentation::SourceEvidenceCollector _sourceEvidenceCollector{};
    };
}
