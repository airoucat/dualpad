#include "pch.h"
#include "input_v2/telemetry/ReplayHarness.h"

#include "input_v2/telemetry/TraceSchema.h"

#include <fstream>
#include <utility>

namespace dualpad::input_v2::telemetry
{
    namespace
    {
        ReplayResult Fail(std::string message)
        {
            return ReplayResult{ false, std::move(message) };
        }

        ReplayResult Pass(std::string message)
        {
            return ReplayResult{ true, std::move(message) };
        }

        std::string ReadFirstLine(const std::filesystem::path& path)
        {
            std::ifstream in(path);
            std::string line;
            std::getline(in, line);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }

        ReplayResult ValidateScenario(const std::filesystem::path& scenarioPath)
        {
            if (!std::filesystem::is_directory(scenarioPath)) {
                return Fail("scenario is not a directory: " + scenarioPath.string());
            }

            for (const auto& spec : Phase0TraceFiles()) {
                const auto filePath = scenarioPath / spec.name;
                if (!std::filesystem::is_regular_file(filePath)) {
                    return Fail("missing trace file: " + filePath.string());
                }

                const auto header = ReadFirstLine(filePath);
                if (header != spec.header) {
                    return Fail("schema header mismatch in " + filePath.string());
                }
            }

            return Pass("scenario schema ok");
        }

        ReplayResult CopyScenario(const std::filesystem::path& scenarioPath, const std::filesystem::path& outputPath)
        {
            std::filesystem::create_directories(outputPath);
            for (const auto& spec : Phase0TraceFiles()) {
                const auto source = scenarioPath / spec.name;
                const auto destination = outputPath / spec.name;
                std::filesystem::copy_file(
                    source,
                    destination,
                    std::filesystem::copy_options::overwrite_existing);
            }

            return Pass("scenario replay materialized: " + outputPath.string());
        }
    }

    ReplayMode ParseReplayMode(std::string_view value)
    {
        if (value == "processor") {
            return ReplayMode::Processor;
        }
        return ReplayMode::Dispatcher;
    }

    ReplayResult ReplayScenario(
        const std::filesystem::path& scenarioPath,
        ReplayMode mode,
        const std::filesystem::path& outputPath)
    {
        (void)mode;

        const auto validation = ValidateScenario(scenarioPath);
        if (!validation.ok) {
            return validation;
        }

        return CopyScenario(scenarioPath, outputPath);
    }

    ReplayResult ReplayBatch(
        const std::filesystem::path& phase0Root,
        ReplayMode mode,
        const std::filesystem::path& outputRoot)
    {
        if (!std::filesystem::is_directory(phase0Root)) {
            return Fail("phase0 root is not a directory: " + phase0Root.string());
        }

        std::size_t replayed = 0;
        for (const auto& entry : std::filesystem::directory_iterator(phase0Root)) {
            if (!entry.is_directory()) {
                continue;
            }

            const auto scenarioName = entry.path().filename();
            const auto result = ReplayScenario(entry.path(), mode, outputRoot / scenarioName);
            if (!result.ok) {
                return result;
            }
            ++replayed;
        }

        return Pass("batch replay materialized scenarios=" + std::to_string(replayed));
    }
}
