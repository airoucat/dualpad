#pragma once

#include "input_v2/presentation/PresentationProjection.h"
#include "input_v2/prompt/PromptScope.h"

namespace dualpad::input_v2::prompt
{
    class PromptProjection
    {
    public:
        PublishedPromptScope BuildPromptScope(
            const presentation::PublishedPresentationState& presentation,
            std::uint64_t manifestEpoch);

        const PublishedPromptScope& GetPublishedPromptScope() const;
        void ResetForTests();

    private:
        PublishedPromptScope _published{};
    };
}
