#pragma once

#include "haptics/SemanticCacheTypes.h"
#include <string_view>

namespace dualpad::haptics
{
    class SemanticRules
    {
    public:
        static SemanticRules& GetSingleton();

        std::uint32_t GetRulesVersion() const { return _rulesVersion; }

        SemanticMeta Classify(
            std::string_view editorId,
            RE::FormType formType) const;

    private:
        SemanticRules() = default;
        std::uint32_t _rulesVersion{ 3 };

        static SemanticMeta FallbackByFormType(RE::FormType t);
    };
}