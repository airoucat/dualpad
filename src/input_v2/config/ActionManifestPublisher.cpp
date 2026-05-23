#include "pch.h"

#include "input_v2/config/ActionManifestPublisher.h"

#include "input_v2/config/AtomicConfigReloader.h"

#include <format>

namespace logger = SKSE::log;

namespace dualpad::input_v2::config
{
    ActionManifestPublisher& ActionManifestPublisher::GetSingleton()
    {
        static ActionManifestPublisher instance;
        return instance;
    }

    void ActionManifestPublisher::PublishPromotedBundle(
        const CompiledConfigBundle& bundle,
        std::uint64_t manifestEpoch)
    {
        // Fail-closed by default: keep publication observable even if caller passes mismatched values.
        if (bundle.manifestEpoch != manifestEpoch ||
            bundle.catalog.manifestEpoch != manifestEpoch ||
            bundle.manifest.manifestEpoch != manifestEpoch ||
            bundle.manifest.legacyBindingProjection.manifestEpoch != manifestEpoch) {
            logger::warn(
                "[DualPad][PH1][Publisher] PublishPromotedBundle epoch mismatch: arg={} bundle={} catalog={} manifest={} projection={}",
                manifestEpoch,
                bundle.manifestEpoch,
                bundle.catalog.manifestEpoch,
                bundle.manifest.manifestEpoch,
                bundle.manifest.legacyBindingProjection.manifestEpoch);
        }

        std::scoped_lock lock(_mutex);
        _lastPublishedEpoch = manifestEpoch;
        ++_publishCount;
        logger::info("[DualPad][PH1][Publisher] Published manifest epoch {}", manifestEpoch);
    }

    std::optional<std::uint64_t> ActionManifestPublisher::GetLastPublishedEpoch() const
    {
        std::scoped_lock lock(_mutex);
        return _lastPublishedEpoch;
    }

    std::uint64_t ActionManifestPublisher::GetPublishCount() const
    {
        std::scoped_lock lock(_mutex);
        return _publishCount;
    }
}

