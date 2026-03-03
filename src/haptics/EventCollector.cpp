#include "pch.h"
#include "haptics/EventCollector.h"
#include "haptics/HapticsTypes.h"
#include "haptics/EventQueue.h"
#include "haptics/HapticsConfig.h"
#include "haptics/FormSemanticCache.h"
#include <mutex>
#include <unordered_map>
#include <SKSE/SKSE.h>
#include <algorithm>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        inline float Clamp(float x, float lo, float hi)
        {
            return std::clamp(x, lo, hi);
        }
        struct DedupState
        {
            std::mutex mtx;
            std::unordered_map<std::uint64_t, std::uint64_t> lastEmitUs;
            std::uint64_t suppressCount{ 0 };
        };
        DedupState g_dedup;
        inline std::uint64_t Hash64(std::uint64_t x)
        {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdULL;
            x ^= x >> 33;
            x *= 0xc4ceb9fe1a85ec53ULL;
            x ^= x >> 33;
            return x;
        }
        inline std::uint64_t DedupWindowUs(EventType t)
        {
            switch (t) {
            case EventType::HitImpact:   return 50'000; // 50ms
            case EventType::WeaponSwing: return 35'000;
            case EventType::Block:       return 45'000;
            case EventType::Footstep:    return 22'000;
            case EventType::Jump:
            case EventType::Land:        return 45'000;
            default:                     return 30'000;
            }
        }
        inline std::uint64_t MakeDedupKey(const EventMsg& m)
        {
            std::uint64_t k = 1469598103934665603ULL;
            auto mix = [&](std::uint64_t v) {
                k ^= Hash64(v + 0x9e3779b97f4a7c15ULL + (k << 6) + (k >> 2));
                };
            mix(static_cast<std::uint64_t>(m.type));
            mix(static_cast<std::uint64_t>(m.actorId));
            mix(static_cast<std::uint64_t>(m.formId));
            mix(static_cast<std::uint64_t>(m.semanticHint));
            return k;
        }
        inline bool PassDedup(const EventMsg& m)
        {
            if (m.qpc == 0) {
                return true;
            }
            const auto key = MakeDedupKey(m);
            const auto winUs = DedupWindowUs(m.type);
            std::scoped_lock lk(g_dedup.mtx);
            if (g_dedup.lastEmitUs.size() > 4096) {
                const auto nowUs = m.qpc;
                for (auto it = g_dedup.lastEmitUs.begin(); it != g_dedup.lastEmitUs.end();) {
                    if (nowUs > it->second && (nowUs - it->second) > 2'000'000) {
                        it = g_dedup.lastEmitUs.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }
            auto it = g_dedup.lastEmitUs.find(key);
            if (it != g_dedup.lastEmitUs.end()) {
                const auto dt = m.qpc - it->second;
                if (dt < winUs) {
                    ++g_dedup.suppressCount;
                    if ((g_dedup.suppressCount % 200) == 0) {
                        logger::info("[Haptics][EventCollector] dedup suppressed={}", g_dedup.suppressCount);
                    }
                    return false;
                }
            }
            g_dedup.lastEmitUs[key] = m.qpc;
            return true;
        }
    }

    EventCollector& EventCollector::GetSingleton()
    {
        static EventCollector instance;
        return instance;
    }

    void EventCollector::Register()
    {
        if (_registered) {
            logger::warn("[Haptics][EventCollector] Already registered");
            return;
        }

        logger::info("[Haptics][EventCollector] Registering event listeners...");

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::error("[Haptics][EventCollector] Player not found");
            return;
        }

        player->AddAnimationGraphEventSink(this);

        auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
        if (eventSource) {
            eventSource->AddEventSink<RE::TESHitEvent>(this);
        }

        _registered = true;
        logger::info("[Haptics][EventCollector] Event listeners registered");
    }

    void EventCollector::Unregister()
    {
        if (!_registered) {
            return;
        }

        logger::info("[Haptics][EventCollector] Unregistering event listeners...");

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->RemoveAnimationGraphEventSink(this);
        }

        auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
        if (eventSource) {
            eventSource->RemoveEventSink<RE::TESHitEvent>(this);
        }

        _registered = false;
        logger::info("[Haptics][EventCollector] Event listeners unregistered");
    }

    RE::BSEventNotifyControl EventCollector::ProcessEvent(
        const RE::BSAnimationGraphEvent* event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
    {
        if (!event || !event->holder) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (!IsPlayerEvent(event->holder)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const std::string_view tag = event->tag.c_str();
        logger::info("[Haptics][EventCollector] Raw animation event: {}", tag);

        EventMsg msg{};
        msg.qpc = ToQPC(Now());
        msg.actorId = event->holder->GetFormID();
        msg.formId = event->holder->GetFormID();
        msg.intensity = 1.0f;

        if (tag.find("footstep") != std::string_view::npos ||
            tag.find("Footstep") != std::string_view::npos ||
            tag.find("FootLeft") != std::string_view::npos ||
            tag.find("FootRight") != std::string_view::npos) {
            msg.type = EventType::Footstep;
            msg.intensity = 0.40f;
        }
        else if (tag.find("weaponSwing") != std::string_view::npos ||
            tag.find("WeaponSwing") != std::string_view::npos ||
            tag.find("weaponLeftSwing") != std::string_view::npos) {
            msg.type = EventType::WeaponSwing;
            msg.intensity = 0.60f;
        }
        else if (tag.find("JumpUp") != std::string_view::npos) {
            msg.type = EventType::Jump;
            msg.intensity = 0.50f;
        }
        else if (tag.find("JumpLandEnd") != std::string_view::npos) {
            msg.type = EventType::Land;
            msg.intensity = 0.70f;
        }
        else if (tag.find("blockStart") != std::string_view::npos) {
            msg.type = EventType::Block;
            msg.intensity = 0.50f;
        }
        else {
            return RE::BSEventNotifyControl::kContinue;
        }

        // FormID语义缓存：O(1) 查语义 + 权重
        const auto sem = FormSemanticCache::GetSingleton().Resolve(msg.formId, msg.type);
        msg.semanticHint = sem.group;
        msg.semanticConfidence = sem.confidence;
        msg.semanticWeight = sem.baseWeight;
        msg.semanticFlags = static_cast<std::uint16_t>(sem.flags);
        msg.intensity = Clamp(msg.intensity * (0.85f + sem.baseWeight* 0.30f), 0.05f, 1.0f);

        auto& config = HapticsConfig::GetSingleton();
        if (!config.IsEventAllowed(msg.type)) {
            logger::info("[Haptics][EventCollector] Event filtered: {} (mode restriction)", ToString(msg.type));
            return RE::BSEventNotifyControl::kContinue;
        }
        if (!PassDedup(msg)) {
            logger::info("[Haptics][EventCollector] Animation dedup: {}", ToString(msg.type));
            return RE::BSEventNotifyControl::kContinue;
        }

        const bool pushed = EventQueue::GetSingleton().Push(msg);
        logger::info("[Haptics][EventCollector] Animation event: {} -> {} (pushed={})",
            tag, ToString(msg.type), pushed);

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl EventCollector::ProcessEvent(
        const RE::TESHitEvent* event,
        RE::BSTEventSource<RE::TESHitEvent>*)
    {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* target = event->target.get();
        auto* cause = event->cause.get();

        const bool playerIsTarget = (target == player);
        const bool playerIsCause = (cause == player);

        if (!playerIsTarget && !playerIsCause) {
            return RE::BSEventNotifyControl::kContinue;
        }

        EventMsg msg{};
        msg.qpc = ToQPC(Now());
        msg.type = EventType::HitImpact;
        msg.actorId = player->GetFormID();
        msg.formId = player->GetFormID();
        msg.intensity = playerIsTarget ? 0.95f : 0.80f;

        const auto sem = FormSemanticCache::GetSingleton().Resolve(msg.formId, msg.type);
        msg.semanticHint = sem.group;
        msg.semanticConfidence = sem.confidence;
        msg.semanticWeight = sem.baseWeight;
        msg.semanticFlags = static_cast<std::uint16_t>(sem.flags);
        msg.intensity = Clamp(msg.intensity * (0.90f + sem.baseWeight* 0.20f), 0.05f, 1.0f);

        auto& config = HapticsConfig::GetSingleton();
        if (!config.IsEventAllowed(msg.type)) {
            logger::info("[Haptics][EventCollector] HitImpact filtered by mode");
            return RE::BSEventNotifyControl::kContinue;
        }
        if (!PassDedup(msg)) {
            logger::info("[Haptics][EventCollector] HitImpact dedup");
            return RE::BSEventNotifyControl::kContinue;
        }
        const bool pushed = EventQueue::GetSingleton().Push(msg);

        logger::info(
            "[Haptics][EventCollector] HitImpact pushed={} playerIsTarget={} playerIsCause={} intensity={:.2f}",
            pushed, playerIsTarget, playerIsCause, msg.intensity);

        return RE::BSEventNotifyControl::kContinue;
    }

    bool EventCollector::IsPlayerEvent(RE::TESObjectREFR* ref) const
    {
        if (!ref) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        return ref == player;
    }
}