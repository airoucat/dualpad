#include "pch.h"
#include "input_v2/telemetry/ReplayHarness.h"

#include "input_v2/telemetry/TraceSchema.h"

#include <fstream>
#include <stdexcept>
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

        ReplayResult MaterializeScenarioFixture(
            const std::filesystem::path& scenarioPath,
            const std::filesystem::path& outputPath)
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

            return Pass("scenario fixture materialized: " + outputPath.string());
        }

        ReplayResult BehavioralReplayNotImplemented(ReplayMode mode)
        {
            const auto modeName = mode == ReplayMode::Processor ? "processor" : "dispatcher";
            return Fail(std::string("behavioral replay mode is not implemented yet: ") + modeName);
        }
    }

    ReplayMode ParseReplayMode(std::string_view value)
    {
        if (value == "validate-schema") {
            return ReplayMode::ValidateSchema;
        }
        if (value == "materialize-fixture") {
            return ReplayMode::MaterializeFixture;
        }
        if (value == "dispatcher") {
            return ReplayMode::Dispatcher;
        }
        if (value == "processor") {
            return ReplayMode::Processor;
        }
        throw std::invalid_argument("unknown replay mode: " + std::string(value));
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

        switch (mode) {
        case ReplayMode::ValidateSchema:
            return Pass("scenario schema validated: " + scenarioPath.string());
        case ReplayMode::MaterializeFixture:
            return MaterializeScenarioFixture(scenarioPath, outputPath);
        case ReplayMode::Dispatcher:
        case ReplayMode::Processor:
            return BehavioralReplayNotImplemented(mode);
        }

        return Fail("unknown replay mode");
    }

    ReplayResult ReplayBatch(
        const std::filesystem::path& phase0Root,
        ReplayMode mode,
        const std::filesystem::path& outputRoot)
    {
        if (!std::filesystem::is_directory(phase0Root)) {
            return Fail("phase0 root is not a directory: " + phase0Root.string());
        }

        std::size_t scenarios = 0;
        for (const auto& entry : std::filesystem::directory_iterator(phase0Root)) {
            if (!entry.is_directory()) {
                continue;
            }

            const auto scenarioName = entry.path().filename();
            const auto result = ReplayScenario(entry.path(), mode, outputRoot / scenarioName);
            if (!result.ok) {
                return result;
            }
            ++scenarios;
        }

        switch (mode) {
        case ReplayMode::ValidateSchema:
            return Pass("batch schema validated scenarios=" + std::to_string(scenarios));
        case ReplayMode::MaterializeFixture:
            return Pass("batch fixtures materialized scenarios=" + std::to_string(scenarios));
        case ReplayMode::Dispatcher:
        case ReplayMode::Processor:
            return BehavioralReplayNotImplemented(mode);
        }

        return Fail("unknown replay mode");
    }
}
