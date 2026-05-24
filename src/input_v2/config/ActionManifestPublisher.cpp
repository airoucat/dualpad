#include "pch.h"

#include "input_v2/config/ActionManifestPublisher.h"

#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/actions/CompiledActionGraphPublisher.h"
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

    bool ActionManifestPublisher::PublishPromotedBundle(
        const CompiledConfigBundle& bundle,
        std::uint64_t manifestEpoch)
    {
        if (bundle.manifestEpoch != manifestEpoch ||
            bundle.catalog.manifestEpoch != manifestEpoch ||
            bundle.manifest.manifestEpoch != manifestEpoch ||
            bundle.manifest.legacyBindingProjection.manifestEpoch != manifestEpoch) {
            logger::error(
                "[DualPad][PH1][Publisher] PublishPromotedBundle epoch mismatch: arg={} bundle={} catalog={} manifest={} projection={}",
                manifestEpoch,
                bundle.manifestEpoch,
                bundle.catalog.manifestEpoch,
                bundle.manifest.manifestEpoch,
                bundle.manifest.legacyBindingProjection.manifestEpoch);
            return false;
        }

        const auto graphCompile = actions::ActionGraphCompiler::Compile(bundle.manifest);
        if (!graphCompile.ok) {
            logger::error(
                "[DualPad][PH4][GraphPublisher] Compile failed for manifest epoch {}: {}",
                manifestEpoch,
                graphCompile.message);
            return false;
        }

        const auto graphPublication =
            actions::CompiledActionGraphPublisher::GetRuntimeOwner().Publish(graphCompile.graph, manifestEpoch);
        if (!graphPublication.ok) {
            logger::error(
                "[DualPad][PH4][GraphPublisher] Publish failed for manifest epoch {}: {}",
                manifestEpoch,
                graphPublication.message);
            return false;
        }

        const auto activeEpochBeforePublish = AtomicConfigReloader::GetSingleton().GetActiveEpoch();

        std::scoped_lock lock(_mutex);
        _lastPublishedEpoch = manifestEpoch;
        _activeEpochObservedAtLastPublish = activeEpochBeforePublish;
        ++_publishCount;
        logger::info("[DualPad][PH1][Publisher] Published manifest epoch {}", manifestEpoch);
        return true;
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

    std::optional<std::uint64_t> ActionManifestPublisher::GetActiveEpochObservedAtLastPublish() const
    {
        std::scoped_lock lock(_mutex);
        return _activeEpochObservedAtLastPublish;
    }

    void ActionManifestPublisher::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _lastPublishedEpoch.reset();
        _activeEpochObservedAtLastPublish.reset();
        _publishCount = 0;
    }
}
