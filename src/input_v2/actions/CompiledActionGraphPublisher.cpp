#include "pch.h"

#include "input_v2/actions/CompiledActionGraphPublisher.h"

namespace dualpad::input_v2::actions
{
    CompiledActionGraphPublisher& CompiledActionGraphPublisher::GetRuntimeOwner()
    {
        static CompiledActionGraphPublisher owner;
        return owner;
    }

    CompiledActionGraphPublication CompiledActionGraphPublisher::Publish(
        const CompiledActionGraph& graph,
        std::uint64_t manifestEpoch)
    {
        if (graph.manifestEpoch != manifestEpoch) {
            const auto snapshot = GetActiveSnapshot();
            return CompiledActionGraphPublication{
                .ok = false,
                .message = "manifest epoch mismatch",
                .graph = snapshot.graph
            };
        }

        auto published = std::make_shared<CompiledActionGraph>(graph);
        {
            std::scoped_lock lock(_mutex);
            _active = PublishedActionGraphSnapshot{
                .manifestEpoch = manifestEpoch,
                .graph = published
            };
        }
        return CompiledActionGraphPublication{
            .ok = true,
            .message = "ok",
            .graph = published
        };
    }

    std::shared_ptr<const CompiledActionGraph> CompiledActionGraphPublisher::GetActiveGraph() const
    {
        return GetActiveSnapshot().graph;
    }

    std::uint64_t CompiledActionGraphPublisher::GetActiveManifestEpoch() const
    {
        return GetActiveSnapshot().manifestEpoch;
    }

    PublishedActionGraphSnapshot CompiledActionGraphPublisher::GetActiveSnapshot() const
    {
        std::scoped_lock lock(_mutex);
        return _active;
    }

    void CompiledActionGraphPublisher::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _active = PublishedActionGraphSnapshot{};
    }
}
