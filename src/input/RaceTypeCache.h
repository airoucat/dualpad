#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace dualpad::input
{
    enum class RaceType : std::uint8_t
    {
        Unknown = 0,
        Other = 1,
        Werewolf = 2,
        VampireLord = 3
    };

    class RaceTypeCache
    {
    public:
        struct Stats
        {
            std::uint64_t builds{ 0 };
            std::uint64_t entries{ 0 };
            std::uint64_t werewolfEntries{ 0 };
            std::uint64_t vampireLordEntries{ 0 };
            std::uint64_t lookups{ 0 };
            std::uint64_t hits{ 0 };
            std::uint64_t misses{ 0 };
        };

        static RaceTypeCache& GetSingleton();

        RaceType Resolve(std::uint32_t raceFormID);

        Stats GetStats() const;
        void Invalidate();

    private:
        RaceTypeCache() = default;

        void EnsureBuilt();
        void BuildLocked();
        static RaceType ClassifyEditorID(std::string_view editorID);

        mutable std::mutex _mutex;
        std::unordered_map<std::uint32_t, RaceType> _map;

        std::atomic<bool> _built{ false };
        std::atomic<std::uint64_t> _builds{ 0 };
        std::atomic<std::uint64_t> _werewolfEntries{ 0 };
        std::atomic<std::uint64_t> _vampireLordEntries{ 0 };
        std::atomic<std::uint64_t> _lookups{ 0 };
        std::atomic<std::uint64_t> _hits{ 0 };
        std::atomic<std::uint64_t> _misses{ 0 };
    };
}
