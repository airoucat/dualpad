#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace dualpad::input_v2::telemetry
{
    enum class ReplayMode
    {
        Dispatcher,
        Processor
    };

    struct ReplayResult
    {
        bool ok{ false };
        std::string message;
    };

    ReplayMode ParseReplayMode(std::string_view value);
    ReplayResult ReplayScenario(
        const std::filesystem::path& scenarioPath,
        ReplayMode mode,
        const std::filesystem::path& outputPath);
    ReplayResult ReplayBatch(
        const std::filesystem::path& phase0Root,
        ReplayMode mode,
        const std::filesystem::path& outputRoot);
}
