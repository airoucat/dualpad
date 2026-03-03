#include "pch.h"
#include "haptics/SemanticRules.h"

#include <algorithm>
#include <string>

namespace dualpad::haptics
{
    namespace
    {
        inline std::string ToLower(std::string_view s)
        {
            std::string out(s.begin(), s.end());
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        inline bool Contains(std::string_view s, std::string_view key)
        {
            return s.find(key) != std::string_view::npos;
        }
    }

    SemanticRules& SemanticRules::GetSingleton()
    {
        static SemanticRules s;
        return s;
    }

    SemanticMeta SemanticRules::Classify(std::string_view editorId, RE::FormType formType) const
    {
        SemanticMeta m = FallbackByFormType(formType);
        std::string id = ToLower(editorId);

        // Hard override（可后续改成 ini/json）
        if (id == "uisoundok" || id == "ui_menu_ok") {
            m.group = SemanticGroup::UI; m.confidence = 0.95f; m.baseWeight = 0.45f;
            m.flags = SemanticFlags::IsUI;
            return m;
        }

        // Exact/Keyword
        if (Contains(id, "foot") || Contains(id, "step")) {
            m.group = SemanticGroup::Footstep; m.confidence = 0.88f; m.baseWeight = 0.55f;
            return m;
        }
        if (Contains(id, "swing") || Contains(id, "slash")) {
            m.group = SemanticGroup::WeaponSwing; m.confidence = 0.90f; m.baseWeight = 0.78f;
            return m;
        }
        if (Contains(id, "hit") || Contains(id, "impact")) {
            m.group = SemanticGroup::Hit; m.confidence = 0.88f; m.baseWeight = 0.85f;
            return m;
        }
        if (Contains(id, "block") || Contains(id, "parry")) {
            m.group = SemanticGroup::Block; m.confidence = 0.85f; m.baseWeight = 0.70f;
            return m;
        }
        if (Contains(id, "bow") || Contains(id, "arrow")) {
            m.group = SemanticGroup::Bow; m.confidence = 0.85f; m.baseWeight = 0.65f;
            return m;
        }
        if (Contains(id, "voice") || Contains(id, "shout") || Contains(id, "dialog")) {
            m.group = SemanticGroup::Voice; m.confidence = 0.82f; m.baseWeight = 0.50f;
            m.flags = m.flags | SemanticFlags::IsVoice;
            return m;
        }
        if (Contains(id, "music")) {
            m.group = SemanticGroup::Music; m.confidence = 0.92f; m.baseWeight = 0.30f;
            m.flags = m.flags | SemanticFlags::IsMusic | SemanticFlags::IsLoop;
            return m;
        }
        if (Contains(id, "ambient") || Contains(id, "wind") || Contains(id, "water")) {
            m.group = SemanticGroup::Ambient; m.confidence = 0.80f; m.baseWeight = 0.25f;
            m.flags = m.flags | SemanticFlags::IsAmbient | SemanticFlags::IsLoop;
            return m;
        }

        return m;
    }

    SemanticMeta SemanticRules::FallbackByFormType(RE::FormType t)
    {
        SemanticMeta m{};
        m.group = SemanticGroup::Unknown;
        m.confidence = 0.45f;
        m.baseWeight = 0.50f;

        // FormType Hint
        if (t == RE::FormType::Sound) {
            m.group = SemanticGroup::Ambient;
            m.confidence = 0.55f;
            m.baseWeight = 0.35f;
        }

        return m;
    }
}