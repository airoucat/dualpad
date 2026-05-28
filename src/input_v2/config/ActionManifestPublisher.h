#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

namespace dualpad::input_v2::config
{
    struct CompiledConfigBundle;

    // Phase 1 publication seam for ManifestEpochChanged.
    //
    // Hard constraint:
    // - ActionManifestPublisher::PublishPromotedBundle(...) is the only producer seam for manifest epoch change markers.
    // - Phase 1 does not yet have IngressHub; we still record the latest published epoch in a test-observable way.
    class ActionManifestPublisher
    {
    public:
        static ActionManifestPublisher& GetSingleton();

        bool PublishPromotedBundle(const CompiledConfigBundle& bundle, std::uint64_t manifestEpoch);

        std::optional<std::uint64_t> GetLastPublishedEpoch() const;
        std::optional<std::uint64_t> GetActiveEpochObservedAtLastPublish() const;
        std::uint64_t GetPublishCount() const;
        void ResetForTests();

    private:
        ActionManifestPublisher() = default;

        mutable std::mutex _mutex;
        std::optional<std::uint64_t> _lastPublishedEpoch;
        std::optional<std::uint64_t> _activeEpochObservedAtLastPublish;
        std::uint64_t _publishCount{ 0 };
    };
}
