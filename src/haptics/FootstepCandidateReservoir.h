#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dualpad::haptics
{
    class FootstepCandidateReservoir
    {
    public:
        enum class Source : std::uint8_t
        {
            Unknown = 0,
            Init,
            Submit,
            Tap
        };

        struct Candidate
        {
            std::uint64_t instanceId{ 0 };
            std::uintptr_t voicePtr{ 0 };
            std::uint32_t generation{ 0 };
            std::uint64_t observedUs{ 0 };
            Source source{ Source::Unknown };
            float confidence{ 0.0f };
        };

        struct Stats
        {
            std::uint64_t observed{ 0 };
            std::uint64_t observedInit{ 0 };
            std::uint64_t observedSubmit{ 0 };
            std::uint64_t observedTap{ 0 };
            std::uint64_t expired{ 0 };
            std::uint64_t snapshotCalls{ 0 };
            std::uint64_t returnedCandidates{ 0 };
            std::uint32_t active{ 0 };
        };

        static FootstepCandidateReservoir& GetSingleton();

        void Reset();
        void ObserveCandidate(
            std::uint64_t instanceId,
            std::uintptr_t voicePtr,
            std::uint32_t generation,
            std::uint64_t observedUs,
            Source source,
            float confidence);
        std::vector<Candidate> SnapshotForTruth(std::uint64_t truthUs, std::size_t maxCount);
        Stats GetStats() const;

    private:
        FootstepCandidateReservoir() = default;

        void ExpireLocked(std::uint64_t nowUs);
        static std::uint32_t SourcePriority(Source source);

        mutable std::mutex _mutex;
        std::vector<Candidate> _candidates{};

        std::atomic<std::uint64_t> _observed{ 0 };
        std::atomic<std::uint64_t> _observedInit{ 0 };
        std::atomic<std::uint64_t> _observedSubmit{ 0 };
        std::atomic<std::uint64_t> _observedTap{ 0 };
        std::atomic<std::uint64_t> _expired{ 0 };
        std::atomic<std::uint64_t> _snapshotCalls{ 0 };
        std::atomic<std::uint64_t> _returnedCandidates{ 0 };
    };
}
