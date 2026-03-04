#pragma once

#include "haptics/HapticsTypes.h"

#include <RE/Skyrim.h>

#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dualpad::haptics
{
    namespace semantic_flags
    {
        inline constexpr std::uint8_t kIsLoop = 1u << 0;
        inline constexpr std::uint8_t kIsAmbient = 1u << 1;
        inline constexpr std::uint8_t kIsVoice = 1u << 2;
        inline constexpr std::uint8_t kIsUI = 1u << 3;
    }

    struct FormSemanticMeta
    {
        SemanticGroup group{ SemanticGroup::Unknown };
        float confidence{ 0.5f };      // 0..1
        float baseWeight{ 0.5f };      // 0..1
        std::uint32_t texturePresetId{ 0 };
        std::uint8_t flags{ 0 };       // semantic_flags bitmask
    };

    class SemanticRuleEngine
    {
    public:
        static SemanticRuleEngine& GetSingleton();

        // Startup path: parse rules from json. Failure keeps defaults (fail-open).
        bool LoadRules(const std::filesystem::path& path);
        std::uint32_t GetRuleVersion() const;

        // Startup/offline classification path.
        FormSemanticMeta ClassifyEditorID(
            std::string_view editorID,
            RE::FormType formType,
            std::uint32_t formID = 0) const;

    private:
        struct KeywordRule
        {
            std::vector<std::string> positive;
            std::vector<std::string> negative;
            float weight{ 1.0f };
        };

        struct RuleSet
        {
            std::uint32_t version{ 1 };
            std::unordered_map<std::uint32_t, FormSemanticMeta> hardOverrides;
            std::unordered_map<std::string, FormSemanticMeta> exactMatch;        // lowercase key
            std::unordered_map<SemanticGroup, KeywordRule> keywordScoring;
            std::unordered_map<std::uint32_t, float> formTypeHints;              // key: FormType underlying value
        };

        SemanticRuleEngine();

        static std::string NormalizeToken(std::string_view s);
        static std::uint8_t FlagsForGroup(SemanticGroup group);
        static FormSemanticMeta FallbackByFormType(RE::FormType formType, const RuleSet& rules);
        void LoadDefaultRulesLocked();

        mutable std::shared_mutex _mx;
        RuleSet _rules;
    };
}

