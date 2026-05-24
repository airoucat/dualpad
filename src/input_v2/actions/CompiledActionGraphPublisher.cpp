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
            return CompiledActionGraphPublication{
                .ok = false,
                .message = "manifest epoch mismatch",
                .graph = _activeGraph
            };
        }

        auto published = std::make_shared<CompiledActionGraph>(graph);
        _activeGraph = published;
        _activeManifestEpoch = manifestEpoch;
        return CompiledActionGraphPublication{
            .ok = true,
            .message = "ok",
            .graph = published
        };
    }

    std::shared_ptr<const CompiledActionGraph> CompiledActionGraphPublisher::GetActiveGraph() const
    {
        return _activeGraph;
    }

    std::uint64_t CompiledActionGraphPublisher::GetActiveManifestEpoch() const
    {
        return _activeManifestEpoch;
    }

    void CompiledActionGraphPublisher::ResetForTests()
    {
        _activeGraph.reset();
        _activeManifestEpoch = 0;
    }
}
