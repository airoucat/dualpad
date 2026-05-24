#pragma once

#include "input_v2/actions/CompiledActionGraph.h"

#include <memory>
#include <string>

namespace dualpad::input_v2::actions
{
    struct CompiledActionGraphPublication
    {
        bool ok{ false };
        std::string message;
        std::shared_ptr<const CompiledActionGraph> graph;
    };

    class CompiledActionGraphPublisher
    {
    public:
        CompiledActionGraphPublication Publish(
            const CompiledActionGraph& graph,
            std::uint64_t manifestEpoch);

        [[nodiscard]] std::shared_ptr<const CompiledActionGraph> GetActiveGraph() const;
        [[nodiscard]] std::uint64_t GetActiveManifestEpoch() const;

    private:
        std::shared_ptr<const CompiledActionGraph> _activeGraph;
        std::uint64_t _activeManifestEpoch{ 0 };
    };
}
