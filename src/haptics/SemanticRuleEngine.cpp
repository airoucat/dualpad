#include "pch.h"
#include "haptics/SemanticRuleEngine.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        struct JsonValue
        {
            enum class Type
            {
                Null,
                Bool,
                Number,
                String,
                Object,
                Array
            };

            using Object = std::unordered_map<std::string, JsonValue>;
            using Array = std::vector<JsonValue>;

            Type type{ Type::Null };
            bool boolValue{ false };
            double numberValue{ 0.0 };
            std::string stringValue;
            Object objectValue;
            Array arrayValue;

            [[nodiscard]] bool IsObject() const { return type == Type::Object; }
            [[nodiscard]] bool IsArray() const { return type == Type::Array; }
            [[nodiscard]] bool IsString() const { return type == Type::String; }
            [[nodiscard]] bool IsNumber() const { return type == Type::Number; }

            [[nodiscard]] const JsonValue* Find(std::string_view key) const
            {
                if (!IsObject()) {
                    return nullptr;
                }

                auto it = objectValue.find(std::string(key));
                return (it != objectValue.end()) ? &it->second : nullptr;
            }
        };

        class JsonParser
        {
        public:
            explicit JsonParser(std::string_view src) :
                _src(src)
            {}

            bool Parse(JsonValue& out, std::string& err)
            {
                SkipWS();
                if (!ParseValue(out, err)) {
                    return false;
                }

                SkipWS();
                if (_pos != _src.size()) {
                    err = "trailing content";
                    return false;
                }

                return true;
            }

        private:
            void SkipWS()
            {
                while (_pos < _src.size()) {
                    const unsigned char c = static_cast<unsigned char>(_src[_pos]);
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                        ++_pos;
                    } else {
                        break;
                    }
                }
            }

            bool ParseValue(JsonValue& out, std::string& err)
            {
                SkipWS();
                if (_pos >= _src.size()) {
                    err = "unexpected eof";
                    return false;
                }

                const char c = _src[_pos];
                if (c == '{') {
                    return ParseObject(out, err);
                }
                if (c == '[') {
                    return ParseArray(out, err);
                }
                if (c == '"') {
                    out.type = JsonValue::Type::String;
                    return ParseString(out.stringValue, err);
                }
                if (c == 't' || c == 'f' || c == 'n') {
                    return ParseLiteral(out, err);
                }
                if (c == '-' || (c >= '0' && c <= '9')) {
                    out.type = JsonValue::Type::Number;
                    return ParseNumber(out.numberValue, err);
                }

                err = "invalid value";
                return false;
            }

            bool ParseObject(JsonValue& out, std::string& err)
            {
                if (!Consume('{')) {
                    err = "expected '{'";
                    return false;
                }

                out = {};
                out.type = JsonValue::Type::Object;

                SkipWS();
                if (Consume('}')) {
                    return true;
                }

                while (true) {
                    std::string key;
                    if (!ParseString(key, err)) {
                        return false;
                    }

                    SkipWS();
                    if (!Consume(':')) {
                        err = "expected ':'";
                        return false;
                    }

                    JsonValue value;
                    if (!ParseValue(value, err)) {
                        return false;
                    }

                    out.objectValue.emplace(std::move(key), std::move(value));

                    SkipWS();
                    if (Consume('}')) {
                        break;
                    }
                    if (!Consume(',')) {
                        err = "expected ',' or '}'";
                        return false;
                    }
                }

                return true;
            }

            bool ParseArray(JsonValue& out, std::string& err)
            {
                if (!Consume('[')) {
                    err = "expected '['";
                    return false;
                }

                out = {};
                out.type = JsonValue::Type::Array;

                SkipWS();
                if (Consume(']')) {
                    return true;
                }

                while (true) {
                    JsonValue item;
                    if (!ParseValue(item, err)) {
                        return false;
                    }
                    out.arrayValue.push_back(std::move(item));

                    SkipWS();
                    if (Consume(']')) {
                        break;
                    }
                    if (!Consume(',')) {
                        err = "expected ',' or ']'";
                        return false;
                    }
                }

                return true;
            }

            bool ParseString(std::string& out, std::string& err)
            {
                if (!Consume('"')) {
                    err = "expected '\"'";
                    return false;
                }

                out.clear();
                while (_pos < _src.size()) {
                    const char c = _src[_pos++];
                    if (c == '"') {
                        return true;
                    }

                    if (c != '\\') {
                        out.push_back(c);
                        continue;
                    }

                    if (_pos >= _src.size()) {
                        err = "invalid escape";
                        return false;
                    }

                    const char esc = _src[_pos++];
                    switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                        if (_pos + 4 > _src.size()) {
                            err = "invalid unicode escape";
                            return false;
                        }
                        _pos += 4;
                        out.push_back('?');
                        break;
                    default:
                        err = "invalid string escape";
                        return false;
                    }
                }

                err = "unterminated string";
                return false;
            }

            bool ParseNumber(double& out, std::string& err)
            {
                const std::size_t start = _pos;

                if (_src[_pos] == '-') {
                    ++_pos;
                }

                if (_pos >= _src.size()) {
                    err = "invalid number";
                    return false;
                }

                if (_src[_pos] == '0') {
                    ++_pos;
                } else if (_src[_pos] >= '1' && _src[_pos] <= '9') {
                    while (_pos < _src.size() && _src[_pos] >= '0' && _src[_pos] <= '9') {
                        ++_pos;
                    }
                } else {
                    err = "invalid number";
                    return false;
                }

                if (_pos < _src.size() && _src[_pos] == '.') {
                    ++_pos;
                    if (_pos >= _src.size() || !std::isdigit(static_cast<unsigned char>(_src[_pos]))) {
                        err = "invalid fraction";
                        return false;
                    }

                    while (_pos < _src.size() && std::isdigit(static_cast<unsigned char>(_src[_pos]))) {
                        ++_pos;
                    }
                }

                if (_pos < _src.size() && (_src[_pos] == 'e' || _src[_pos] == 'E')) {
                    ++_pos;
                    if (_pos < _src.size() && (_src[_pos] == '+' || _src[_pos] == '-')) {
                        ++_pos;
                    }
                    if (_pos >= _src.size() || !std::isdigit(static_cast<unsigned char>(_src[_pos]))) {
                        err = "invalid exponent";
                        return false;
                    }
                    while (_pos < _src.size() && std::isdigit(static_cast<unsigned char>(_src[_pos]))) {
                        ++_pos;
                    }
                }

                const std::string num(_src.substr(start, _pos - start));
                char* endPtr = nullptr;
                out = std::strtod(num.c_str(), &endPtr);
                if (!endPtr || *endPtr != '\0') {
                    err = "failed to parse number";
                    return false;
                }

                return true;
            }

            bool ParseLiteral(JsonValue& out, std::string& err)
            {
                if (StartsWith("true")) {
                    out.type = JsonValue::Type::Bool;
                    out.boolValue = true;
                    _pos += 4;
                    return true;
                }
                if (StartsWith("false")) {
                    out.type = JsonValue::Type::Bool;
                    out.boolValue = false;
                    _pos += 5;
                    return true;
                }
                if (StartsWith("null")) {
                    out.type = JsonValue::Type::Null;
                    _pos += 4;
                    return true;
                }

                err = "invalid literal";
                return false;
            }

            bool Consume(char expected)
            {
                SkipWS();
                if (_pos < _src.size() && _src[_pos] == expected) {
                    ++_pos;
                    return true;
                }
                return false;
            }

            bool StartsWith(std::string_view token) const
            {
                return (_pos + token.size() <= _src.size()) &&
                    _src.substr(_pos, token.size()) == token;
            }

            std::string_view _src;
            std::size_t _pos{ 0 };
        };

        std::string ToLowerCopy(std::string_view s)
        {
            std::string out(s.begin(), s.end());
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        std::optional<float> ReadFloat(const JsonValue* v)
        {
            if (!v || !v->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<float>(v->numberValue);
        }

        std::optional<std::uint32_t> ReadU32(const JsonValue* v)
        {
            if (!v || !v->IsNumber()) {
                return std::nullopt;
            }

            if (v->numberValue < 0.0 || v->numberValue > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(v->numberValue);
        }

        std::optional<SemanticGroup> ParseSemanticGroup(std::string_view raw)
        {
            const auto k = ToLowerCopy(raw);

            if (k == "weaponswing" || k == "swing") return SemanticGroup::WeaponSwing;
            if (k == "hit" || k == "impact") return SemanticGroup::Hit;
            if (k == "block") return SemanticGroup::Block;
            if (k == "footstep" || k == "footsteps") return SemanticGroup::Footstep;
            if (k == "bow") return SemanticGroup::Bow;
            if (k == "voice") return SemanticGroup::Voice;
            if (k == "ui") return SemanticGroup::UI;
            if (k == "music") return SemanticGroup::Music;
            if (k == "ambient") return SemanticGroup::Ambient;
            if (k == "unknown") return SemanticGroup::Unknown;

            return std::nullopt;
        }

        std::optional<std::uint32_t> ParseFormIDKey(std::string_view key)
        {
            auto s = ToLowerCopy(key);
            s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }), s.end());
            if (s.empty()) {
                return std::nullopt;
            }

            try {
                const std::size_t base = (s.rfind("0x", 0) == 0) ? 16 : 10;
                return static_cast<std::uint32_t>(std::stoul(s, nullptr, static_cast<int>(base)));
            } catch (...) {
                return std::nullopt;
            }
        }

        std::optional<std::uint32_t> ParseFormTypeKey(std::string_view key)
        {
            std::string k = ToLowerCopy(key);
            k.erase(std::remove_if(k.begin(), k.end(),
                [](unsigned char c) { return !std::isalnum(c); }), k.end());

            static const std::unordered_map<std::string, RE::FormType> map = {
                { "ksound", RE::FormType::Sound },
                { "sound", RE::FormType::Sound },
                { "ksoundrecord", RE::FormType::SoundRecord },
                { "soundrecord", RE::FormType::SoundRecord },
                { "sounddescriptor", RE::FormType::SoundRecord },
                { "ksoundcategory", RE::FormType::SoundCategory },
                { "soundcategory", RE::FormType::SoundCategory },
                { "kmusictype", RE::FormType::MusicType },
                { "musictype", RE::FormType::MusicType },
                { "music", RE::FormType::MusicType },
                { "kmusictrack", RE::FormType::MusicTrack },
                { "musictrack", RE::FormType::MusicTrack },
                { "kacousticspace", RE::FormType::AcousticSpace },
                { "acousticspace", RE::FormType::AcousticSpace },
                { "kfootstep", RE::FormType::Footstep },
                { "footstep", RE::FormType::Footstep },
                { "kfootstepset", RE::FormType::FootstepSet },
                { "footstepset", RE::FormType::FootstepSet },
                { "kimpact", RE::FormType::Impact },
                { "impact", RE::FormType::Impact },
                { "kimpactdataset", RE::FormType::ImpactDataSet },
                { "impactdataset", RE::FormType::ImpactDataSet }
            };

            if (auto it = map.find(k); it != map.end()) {
                return static_cast<std::uint32_t>(it->second);
            }

            try {
                const std::size_t base = (k.rfind("0x", 0) == 0) ? 16 : 10;
                return static_cast<std::uint32_t>(std::stoul(k, nullptr, static_cast<int>(base)));
            } catch (...) {
                return std::nullopt;
            }
        }
    }

    SemanticRuleEngine& SemanticRuleEngine::GetSingleton()
    {
        static SemanticRuleEngine s;
        return s;
    }

    SemanticRuleEngine::SemanticRuleEngine()
    {
        std::unique_lock lk(_mx);
        LoadDefaultRulesLocked();
    }

    bool SemanticRuleEngine::LoadRules(const std::filesystem::path& path)
    {
        if (path.empty()) {
            logger::warn("[Haptics][SemanticRuleEngine] empty rule path, keep defaults");
            return false;
        }

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            logger::warn("[Haptics][SemanticRuleEngine] rule file not found: {}, keep defaults",
                path.string());
            return false;
        }

        std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        if (text.empty()) {
            logger::warn("[Haptics][SemanticRuleEngine] rule file empty: {}, keep defaults",
                path.string());
            return false;
        }

        JsonValue root;
        std::string err;
        JsonParser parser(text);
        if (!parser.Parse(root, err) || !root.IsObject()) {
            logger::warn("[Haptics][SemanticRuleEngine] parse failed: {} ({})", path.string(), err);
            return false;
        }

        RuleSet nextRules{};
        {
            std::shared_lock lk(_mx);
            nextRules = _rules;
        }

        const auto parseMeta = [&](const JsonValue& node, FormSemanticMeta fallback) -> FormSemanticMeta {
            if (!node.IsObject()) {
                return fallback;
            }

            FormSemanticMeta out = fallback;
            bool hasFlags = false;

            if (const auto* g = node.Find("group"); g && g->IsString()) {
                if (const auto parsed = ParseSemanticGroup(g->stringValue); parsed.has_value()) {
                    out.group = *parsed;
                }
            }

            if (const auto v = ReadFloat(node.Find("confidence")); v.has_value()) {
                out.confidence = std::clamp(*v, 0.0f, 1.0f);
            }
            if (const auto v = ReadFloat(node.Find("baseWeight")); v.has_value()) {
                out.baseWeight = std::clamp(*v, 0.0f, 1.0f);
            } else if (const auto v = ReadFloat(node.Find("weight")); v.has_value()) {
                out.baseWeight = std::clamp(*v, 0.0f, 1.0f);
            }
            if (const auto v = ReadU32(node.Find("texturePresetId")); v.has_value()) {
                out.texturePresetId = *v;
            }
            if (const auto v = ReadU32(node.Find("flags")); v.has_value()) {
                out.flags = static_cast<std::uint8_t>(*v & 0xFFu);
                hasFlags = true;
            }

            if (!hasFlags) {
                out.flags = FlagsForGroup(out.group);
            }
            return out;
        };

        auto mergeHardOverrides = [&](const JsonValue& hardNode, bool clearFirst, std::size_t* mergedCount) {
            if (!hardNode.IsObject()) {
                return;
            }
            if (clearFirst) {
                nextRules.hardOverrides.clear();
            }

            for (const auto& [k, v] : hardNode.objectValue) {
                const auto formID = ParseFormIDKey(k);
                if (!formID.has_value()) {
                    continue;
                }
                nextRules.hardOverrides[*formID] = parseMeta(v, FormSemanticMeta{});
                if (mergedCount) {
                    ++(*mergedCount);
                }
            }
        };

        if (const auto v = ReadU32(root.Find("version")); v.has_value()) {
            nextRules.version = *v;
        }

        if (const auto* hard = root.Find("hardOverrides"); hard && hard->IsObject()) {
            mergeHardOverrides(*hard, true, nullptr);
        }

        if (const auto* exact = root.Find("exactMatch"); exact && exact->IsObject()) {
            nextRules.exactMatch.clear();
            for (const auto& [k, v] : exact->objectValue) {
                const auto key = NormalizeToken(k);
                if (key.empty()) {
                    continue;
                }
                nextRules.exactMatch[key] = parseMeta(v, FormSemanticMeta{});
            }
        }

        if (const auto* keyword = root.Find("keywordScoring"); keyword && keyword->IsObject()) {
            nextRules.keywordScoring.clear();
            for (const auto& [groupKey, ruleNode] : keyword->objectValue) {
                const auto group = ParseSemanticGroup(groupKey);
                if (!group.has_value() || !ruleNode.IsObject()) {
                    continue;
                }

                KeywordRule rule{};
                if (const auto* positive = ruleNode.Find("positive"); positive && positive->IsArray()) {
                    for (const auto& item : positive->arrayValue) {
                        if (item.IsString()) {
                            const auto token = NormalizeToken(item.stringValue);
                            if (!token.empty()) {
                                rule.positive.push_back(token);
                            }
                        }
                    }
                }

                if (const auto* negative = ruleNode.Find("negative"); negative && negative->IsArray()) {
                    for (const auto& item : negative->arrayValue) {
                        if (item.IsString()) {
                            const auto token = NormalizeToken(item.stringValue);
                            if (!token.empty()) {
                                rule.negative.push_back(token);
                            }
                        }
                    }
                }

                if (const auto w = ReadFloat(ruleNode.Find("weight")); w.has_value()) {
                    rule.weight = std::clamp(*w, 0.0f, 1.0f);
                }

                if (!rule.positive.empty()) {
                    nextRules.keywordScoring[*group] = std::move(rule);
                }
            }
        }

        if (const auto* hints = root.Find("formTypeHints"); hints && hints->IsObject()) {
            nextRules.formTypeHints.clear();
            for (const auto& [k, v] : hints->objectValue) {
                const auto formType = ParseFormTypeKey(k);
                const auto hint = ReadFloat(&v);
                if (!formType.has_value() || !hint.has_value()) {
                    continue;
                }

                nextRules.formTypeHints[*formType] = std::clamp(*hint, 0.0f, 1.0f);
            }
        }

        std::size_t externalHardMerged = 0;
        const auto externalOverridePath = path.parent_path() / "DualPadSemanticOverrides.json";
        if (!externalOverridePath.empty() && std::filesystem::exists(externalOverridePath)) {
            std::ifstream ofs(externalOverridePath, std::ios::binary);
            std::string otext((std::istreambuf_iterator<char>(ofs)), std::istreambuf_iterator<char>());
            JsonValue overRoot{};
            std::string overErr;
            JsonParser overParser(otext);
            if (!otext.empty() && overParser.Parse(overRoot, overErr) && overRoot.IsObject()) {
                if (const auto* hard = overRoot.Find("hardOverrides"); hard && hard->IsObject()) {
                    mergeHardOverrides(*hard, false, &externalHardMerged);
                }
            }
            else {
                logger::warn(
                    "[Haptics][SemanticRuleEngine] external override parse failed: {} ({})",
                    externalOverridePath.string(),
                    overErr);
            }
        }

        const auto version = nextRules.version;
        const auto hardCount = nextRules.hardOverrides.size();
        const auto exactCount = nextRules.exactMatch.size();
        const auto keywordCount = nextRules.keywordScoring.size();
        const auto hintCount = nextRules.formTypeHints.size();

        {
            std::unique_lock lk(_mx);
            _rules = std::move(nextRules);
        }

        logger::info(
            "[Haptics][SemanticRuleEngine] loaded rules path={} version={} hard={} exact={} keyword={} formHints={} extHard={}",
            path.string(),
            version,
            hardCount,
            exactCount,
            keywordCount,
            hintCount,
            externalHardMerged);
        return true;
    }

    std::uint32_t SemanticRuleEngine::GetRuleVersion() const
    {
        std::shared_lock lk(_mx);
        return _rules.version;
    }

    FormSemanticMeta SemanticRuleEngine::ClassifyEditorID(
        std::string_view editorID,
        RE::FormType formType,
        std::uint32_t formID) const
    {
        std::shared_lock lk(_mx);
        const auto& rules = _rules;

        if (formID != 0) {
            if (auto it = rules.hardOverrides.find(formID); it != rules.hardOverrides.end()) {
                return it->second;
            }
        }

        FormSemanticMeta best = FallbackByFormType(formType, rules);
        const auto normalizedID = NormalizeToken(editorID);
        if (normalizedID.empty()) {
            return best;
        }

        if (auto it = rules.exactMatch.find(normalizedID); it != rules.exactMatch.end()) {
            return it->second;
        }

        float bestScore = -1.0f;
        for (const auto& [group, rule] : rules.keywordScoring) {
            if (rule.positive.empty()) {
                continue;
            }

            std::uint32_t posHits = 0;
            for (const auto& token : rule.positive) {
                if (!token.empty() && normalizedID.find(token) != std::string::npos) {
                    ++posHits;
                }
            }
            if (posHits == 0) {
                continue;
            }

            std::uint32_t negHits = 0;
            for (const auto& token : rule.negative) {
                if (!token.empty() && normalizedID.find(token) != std::string::npos) {
                    ++negHits;
                }
            }

            const float score = rule.weight *
                (static_cast<float>(posHits) - 0.80f * static_cast<float>(negHits));
            if (score <= bestScore) {
                continue;
            }

            bestScore = score;
            best.group = group;
            best.confidence = std::clamp(
                0.48f + 0.16f * static_cast<float>(posHits) -
                0.12f * static_cast<float>(negHits) +
                0.20f * rule.weight,
                0.0f, 1.0f);
            best.baseWeight = std::clamp(0.30f + 0.60f * rule.weight, 0.0f, 1.0f);
            best.flags = FlagsForGroup(group);
        }

        return best;
    }

    std::string SemanticRuleEngine::NormalizeToken(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            if (std::isalnum(c)) {
                out.push_back(static_cast<char>(std::tolower(c)));
            }
        }
        return out;
    }

    std::uint8_t SemanticRuleEngine::FlagsForGroup(SemanticGroup group)
    {
        switch (group) {
        case SemanticGroup::Ambient:
            return semantic_flags::kIsAmbient | semantic_flags::kIsLoop;
        case SemanticGroup::Music:
            return semantic_flags::kIsLoop;
        case SemanticGroup::Voice:
            return semantic_flags::kIsVoice;
        case SemanticGroup::UI:
            return semantic_flags::kIsUI;
        default:
            return 0;
        }
    }

    FormSemanticMeta SemanticRuleEngine::FallbackByFormType(RE::FormType formType, const RuleSet& rules)
    {
        FormSemanticMeta meta{};
        meta.group = SemanticGroup::Unknown;
        meta.confidence = 0.40f;
        meta.baseWeight = 0.50f;
        meta.texturePresetId = 0;
        meta.flags = 0;

        switch (formType) {
        case RE::FormType::Sound:
        case RE::FormType::SoundRecord:
        case RE::FormType::SoundCategory:
        case RE::FormType::AcousticSpace:
            // Generic sound records are too broad to default to a haptic-bearing ambient class.
            meta.group = SemanticGroup::Unknown;
            meta.confidence = 0.32f;
            meta.baseWeight = 0.20f;
            meta.flags = 0;
            break;
        case RE::FormType::MusicType:
        case RE::FormType::MusicTrack:
            meta.group = SemanticGroup::Music;
            meta.confidence = 0.60f;
            meta.baseWeight = 0.24f;
            meta.flags = semantic_flags::kIsLoop;
            break;
        case RE::FormType::Footstep:
        case RE::FormType::FootstepSet:
            meta.group = SemanticGroup::Footstep;
            meta.confidence = 0.55f;
            meta.baseWeight = 0.58f;
            break;
        case RE::FormType::Impact:
        case RE::FormType::ImpactDataSet:
            meta.group = SemanticGroup::Hit;
            meta.confidence = 0.58f;
            meta.baseWeight = 0.80f;
            break;
        default:
            break;
        }

        if (auto it = rules.formTypeHints.find(static_cast<std::uint32_t>(formType));
            it != rules.formTypeHints.end()) {
            meta.confidence = std::clamp(std::max(meta.confidence, it->second), 0.0f, 1.0f);
        }

        return meta;
    }

    void SemanticRuleEngine::LoadDefaultRulesLocked()
    {
        _rules = {};
        _rules.version = 1;

        auto makeMeta = [](SemanticGroup group, float conf, float weight, std::uint32_t tex) {
            FormSemanticMeta m{};
            m.group = group;
            m.confidence = std::clamp(conf, 0.0f, 1.0f);
            m.baseWeight = std::clamp(weight, 0.0f, 1.0f);
            m.texturePresetId = tex;
            m.flags = FlagsForGroup(group);
            return m;
        };

        _rules.exactMatch.emplace("wpnswingsd", makeMeta(SemanticGroup::WeaponSwing, 0.95f, 0.78f, 1));
        _rules.exactMatch.emplace("npchumanfootstepwalk", makeMeta(SemanticGroup::Footstep, 0.90f, 0.56f, 2));

        _rules.keywordScoring.emplace(
            SemanticGroup::WeaponSwing,
            KeywordRule{ { "swing", "swish", "whoosh" }, { "impact", "hit" }, 0.80f });
        _rules.keywordScoring.emplace(
            SemanticGroup::Hit,
            KeywordRule{ { "impact", "hit", "strike", "smash" }, { "swing", "swish" }, 1.00f });
        _rules.keywordScoring.emplace(
            SemanticGroup::Footstep,
            KeywordRule{ { "foot", "step", "walk", "run", "land" }, { "ui", "music" }, 0.72f });
        _rules.keywordScoring.emplace(
            SemanticGroup::Block,
            KeywordRule{ { "block", "parry", "guard" }, { "hit" }, 0.78f });
        _rules.keywordScoring.emplace(
            SemanticGroup::Bow,
            KeywordRule{ { "bow", "arrow", "draw", "release" }, { "magic" }, 0.75f });
        _rules.keywordScoring.emplace(
            SemanticGroup::Voice,
            KeywordRule{ { "voice", "shout", "dialog", "npc" }, { "music" }, 0.70f });
        _rules.keywordScoring.emplace(
            SemanticGroup::UI,
            KeywordRule{ { "ui", "menu", "hud", "cursor" }, { "music", "ambient" }, 0.65f });
        _rules.keywordScoring.emplace(
            SemanticGroup::Music,
            KeywordRule{ { "music", "mus" }, { "ui", "voice", "impact" }, 0.50f });
        _rules.keywordScoring.emplace(
            SemanticGroup::Ambient,
            KeywordRule{ { "ambient", "wind", "water", "rain", "loop" }, { "hit", "swing", "foot" }, 0.45f });

        _rules.formTypeHints[static_cast<std::uint32_t>(RE::FormType::Sound)] = 0.50f;
        _rules.formTypeHints[static_cast<std::uint32_t>(RE::FormType::SoundRecord)] = 0.55f;
        _rules.formTypeHints[static_cast<std::uint32_t>(RE::FormType::SoundCategory)] = 0.30f;
        _rules.formTypeHints[static_cast<std::uint32_t>(RE::FormType::MusicType)] = 0.60f;
        _rules.formTypeHints[static_cast<std::uint32_t>(RE::FormType::MusicTrack)] = 0.55f;
        _rules.formTypeHints[static_cast<std::uint32_t>(RE::FormType::FootstepSet)] = 0.62f;
        _rules.formTypeHints[static_cast<std::uint32_t>(RE::FormType::ImpactDataSet)] = 0.66f;
    }
}
