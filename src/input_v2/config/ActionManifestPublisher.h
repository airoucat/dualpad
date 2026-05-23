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

        void PublishPromotedBundle(const CompiledConfigBundle& bundle, std::uint64_t manifestEpoch);

        std::optional<std::uint64_t> GetLastPublishedEpoch() const;
        std::uint64_t GetPublishCount() const;

    private:
        ActionManifestPublisher() = default;

        mutable std::mutex _mutex;
        std::optional<std::uint64_t> _lastPublishedEpoch;
        std::uint64_t _publishCount{ 0 };
    };
}

