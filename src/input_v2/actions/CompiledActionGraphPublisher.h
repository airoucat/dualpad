#pragma once

#include "input_v2/actions/CompiledActionGraph.h"

#include <memory>
#include <mutex>
#include <string>

namespace dualpad::input_v2::actions
{
    struct CompiledActionGraphPublication
    {
        bool ok{ false };
        std::string message;
        std::shared_ptr<const CompiledActionGraph> graph;
    };

    struct PublishedActionGraphSnapshot
    {
        std::uint64_t manifestEpoch{ 0 };
        std::shared_ptr<const CompiledActionGraph> graph;
    };

    class CompiledActionGraphPublisher
    {
    public:
        static CompiledActionGraphPublisher& GetRuntimeOwner();

        CompiledActionGraphPublication Publish(
            const CompiledActionGraph& graph,
            std::uint64_t manifestEpoch);

        [[nodiscard]] std::shared_ptr<const CompiledActionGraph> GetActiveGraph() const;
        [[nodiscard]] std::uint64_t GetActiveManifestEpoch() const;
        [[nodiscard]] PublishedActionGraphSnapshot GetActiveSnapshot() const;
        void ResetForTests();

    private:
        mutable std::mutex _mutex;
        PublishedActionGraphSnapshot _active;
    };
}
