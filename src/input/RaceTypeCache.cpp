#include "pch.h"
#include "input/RaceTypeCache.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        std::string ToLower(std::string_view s)
        {
            std::string out;
            out.reserve(s.size());
            for (const auto c : s) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return out;
        }
    }

    RaceTypeCache& RaceTypeCache::GetSingleton()
    {
        static RaceTypeCache s;
        return s;
    }

    RaceType RaceTypeCache::ClassifyEditorID(std::string_view editorID)
    {
        if (editorID.empty()) {
            return RaceType::Unknown;
        }

        const auto lower = ToLower(editorID);

        if (lower.find("werewolf") != std::string::npos) {
            return RaceType::Werewolf;
        }

        if (lower.find("vampirelord") != std::string::npos ||
            (lower.find("vampire") != std::string::npos && lower.find("lord") != std::string::npos)) {
            return RaceType::VampireLord;
        }

        return RaceType::Other;
    }

    void RaceTypeCache::BuildLocked()
    {
        _map.clear();
        _werewolfEntries.store(0, std::memory_order_relaxed);
        _vampireLordEntries.store(0, std::memory_order_relaxed);

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::warn("[DualPad][RaceTypeCache] TESDataHandler is null, using empty cache");
            _builds.fetch_add(1, std::memory_order_relaxed);
            _built.store(true, std::memory_order_release);
            return;
        }

        auto& races = dh->GetFormArray<RE::TESRace>();
        _map.reserve(races.size());

        for (auto* race : races) {
            if (!race) {
                continue;
            }

            const auto formID = race->GetFormID();
            if (formID == 0) {
                continue;
            }

            const char* editorID = race->GetFormEditorID();
            const auto type = ClassifyEditorID(editorID ? std::string_view(editorID) : std::string_view{});
            _map[formID] = type;

            if (type == RaceType::Werewolf) {
                _werewolfEntries.fetch_add(1, std::memory_order_relaxed);
            }
            else if (type == RaceType::VampireLord) {
                _vampireLordEntries.fetch_add(1, std::memory_order_relaxed);
            }
        }

        _builds.fetch_add(1, std::memory_order_relaxed);
        _built.store(true, std::memory_order_release);

        logger::info(
            "[DualPad][RaceTypeCache] built entries={} werewolf={} vampireLord={}",
            _map.size(),
            _werewolfEntries.load(std::memory_order_relaxed),
            _vampireLordEntries.load(std::memory_order_relaxed));
    }

    void RaceTypeCache::EnsureBuilt()
    {
        if (_built.load(std::memory_order_acquire)) {
            return;
        }

        std::scoped_lock lock(_mutex);
        if (_built.load(std::memory_order_relaxed)) {
            return;
        }
        BuildLocked();
    }

    RaceType RaceTypeCache::Resolve(std::uint32_t raceFormID)
    {
        _lookups.fetch_add(1, std::memory_order_relaxed);

        if (raceFormID == 0) {
            _misses.fetch_add(1, std::memory_order_relaxed);
            return RaceType::Unknown;
        }

        EnsureBuilt();

        std::scoped_lock lock(_mutex);
        const auto it = _map.find(raceFormID);
        if (it == _map.end()) {
            _misses.fetch_add(1, std::memory_order_relaxed);
            return RaceType::Unknown;
        }

        _hits.fetch_add(1, std::memory_order_relaxed);
        return it->second;
    }

    RaceTypeCache::Stats RaceTypeCache::GetStats() const
    {
        Stats s{};
        std::scoped_lock lock(_mutex);
        s.builds = _builds.load(std::memory_order_relaxed);
        s.entries = static_cast<std::uint64_t>(_map.size());
        s.werewolfEntries = _werewolfEntries.load(std::memory_order_relaxed);
        s.vampireLordEntries = _vampireLordEntries.load(std::memory_order_relaxed);
        s.lookups = _lookups.load(std::memory_order_relaxed);
        s.hits = _hits.load(std::memory_order_relaxed);
        s.misses = _misses.load(std::memory_order_relaxed);
        return s;
    }

    void RaceTypeCache::Invalidate()
    {
        std::scoped_lock lock(_mutex);
        _map.clear();
        _built.store(false, std::memory_order_release);
    }
}
