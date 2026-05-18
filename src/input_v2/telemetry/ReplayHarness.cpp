#include "pch.h"
#include "input_v2/telemetry/ReplayHarness.h"

#include "input_v2/telemetry/TraceSchema.h"

#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

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

        using CsvRow = std::vector<std::string>;
        using CsvRows = std::vector<CsvRow>;
        using CsvBundleRows = std::map<std::string, CsvRows>;
        using RowsBySequence = std::unordered_map<std::uint64_t, CsvRows>;

        std::vector<std::string> SplitCsvLine(std::string_view line)
        {
            std::vector<std::string> values;
            std::string current;
            for (const char ch : line) {
                if (ch == ',') {
                    values.push_back(current);
                    current.clear();
                    continue;
                }
                current.push_back(ch);
            }
            values.push_back(current);
            return values;
        }

        std::string JoinCsvRow(const CsvRow& row)
        {
            std::ostringstream out;
            for (std::size_t i = 0; i < row.size(); ++i) {
                if (i != 0) {
                    out << ',';
                }
                out << row[i];
            }
            return out.str();
        }

        std::uint64_t ParseU64(const std::string& value, std::string_view field)
        {
            try {
                return static_cast<std::uint64_t>(std::stoull(value));
            } catch (const std::exception&) {
                throw std::runtime_error("invalid uint64 field " + std::string(field) + ": " + value);
            }
        }

        std::uint32_t ParseU32(const std::string& value, std::string_view field)
        {
            try {
                return static_cast<std::uint32_t>(std::stoul(value));
            } catch (const std::exception&) {
                throw std::runtime_error("invalid uint32 field " + std::string(field) + ": " + value);
            }
        }

        std::size_t ParseSize(const std::string& value, std::string_view field)
        {
            try {
                return static_cast<std::size_t>(std::stoull(value));
            } catch (const std::exception&) {
                throw std::runtime_error("invalid size field " + std::string(field) + ": " + value);
            }
        }

        float ParseFloat(const std::string& value)
        {
            try {
                return std::stof(value);
            } catch (const std::exception&) {
                return 0.0f;
            }
        }

        std::string ToString(std::uint64_t value)
        {
            return std::to_string(value);
        }

        std::string ToString(std::uint32_t value)
        {
            return std::to_string(value);
        }

        std::string ToString(bool value)
        {
            return value ? "true" : "false";
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

        CsvRows ReadCsvRows(const std::filesystem::path& path)
        {
            std::ifstream in(path);
            std::string line;
            CsvRows rows;
            if (!std::getline(in, line)) {
                return rows;
            }

            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    continue;
                }
                rows.push_back(SplitCsvLine(line));
            }
            return rows;
        }

        CsvRows ReadScenarioRows(const std::filesystem::path& scenarioPath, std::string_view fileName)
        {
            return ReadCsvRows(scenarioPath / std::string(fileName));
        }

        CsvRows& OutputRows(CsvBundleRows& bundle, std::string_view fileName)
        {
            return bundle[std::string(fileName)];
        }

        void WriteCsvRows(
            const std::filesystem::path& outputPath,
            const TraceFileSpec& spec,
            const CsvRows& rows)
        {
            std::filesystem::create_directories(outputPath);
            std::ofstream out(outputPath / std::string(spec.name), std::ios::trunc);
            out << spec.header << '\n';
            for (const auto& row : rows) {
                out << JoinCsvRow(row) << '\n';
            }
        }

        void WriteGeneratedBundle(
            const std::filesystem::path& outputPath,
            const CsvBundleRows& bundle)
        {
            std::filesystem::remove_all(outputPath);
            std::filesystem::create_directories(outputPath);
            for (const auto& spec : Phase0TraceFiles()) {
                const auto found = bundle.find(std::string(spec.name));
                if (found != bundle.end()) {
                    WriteCsvRows(outputPath, spec, found->second);
                } else {
                    WriteCsvRows(outputPath, spec, {});
                }
            }
        }

        std::unordered_map<std::uint64_t, CsvRow> FramesBySequence(const CsvRows& frames)
        {
            std::unordered_map<std::uint64_t, CsvRow> bySequence;
            for (const auto& row : frames) {
                if (row.size() < 15) {
                    throw std::runtime_error("frame row has too few columns");
                }
                bySequence.emplace(ParseU64(row[0], "sequence"), row);
            }
            return bySequence;
        }

        RowsBySequence EventsBySequence(const CsvRows& events)
        {
            RowsBySequence bySequence;
            for (const auto& row : events) {
                if (row.size() < 16) {
                    throw std::runtime_error("event row has too few columns");
                }
                bySequence[ParseU64(row[0], "sequence")].push_back(row);
            }
            return bySequence;
        }

        void AppendEventsForSequence(CsvRows& destination, const RowsBySequence& events, std::uint64_t sequence)
        {
            const auto found = events.find(sequence);
            if (found == events.end()) {
                return;
            }
            destination.insert(destination.end(), found->second.begin(), found->second.end());
        }

        CsvRows GeneratePollRows(const CsvRows& processedFrames, const CsvRows& processedEvents)
        {
            const auto eventsBySequence = EventsBySequence(processedEvents);
            CsvRows pollRows;
            std::uint32_t previousDownMask = 0;
            std::uint32_t unmanagedHeldDown = 0;
            std::uint64_t pollSequence = 0;

            for (const auto& frame : processedFrames) {
                if (frame.size() < 15) {
                    throw std::runtime_error("processed frame row has too few columns");
                }

                ++pollSequence;
                const auto sequence = ParseU64(frame[0], "sequence");
                const auto sourceTimestampUs = frame[2];
                const auto& context = frame[3];
                const auto& contextEpoch = frame[4];
                const bool overflowed = frame[5] == "true";
                const bool coalesced = frame[6] == "true";
                const auto downMask = ParseU32(frame[8], "digital_mask");
                const auto pressedMask = downMask & ~previousDownMask;
                const auto releasedMask = previousDownMask & ~downMask;

                std::uint32_t transientPressedMask = 0;
                std::uint32_t transientReleasedMask = 0;
                const auto events = eventsBySequence.find(sequence);
                if (events != eventsBySequence.end()) {
                    for (const auto& event : events->second) {
                        const auto code = ParseU32(event[4], "code");
                        if (event[2] == "ButtonPress") {
                            transientPressedMask |= code;
                        } else if (event[2] == "ButtonRelease") {
                            transientReleasedMask |= code;
                        }
                    }
                }

                const auto pulseMask = (transientPressedMask & transientReleasedMask) & ~downMask;
                if (releasedMask != 0) {
                    unmanagedHeldDown &= ~releasedMask;
                }
                if (pressedMask != 0) {
                    unmanagedHeldDown |= pressedMask;
                }
                const auto unmanagedDownMask = unmanagedHeldDown | pulseMask;
                const auto hasDigital =
                    unmanagedDownMask != 0 ||
                    pressedMask != 0 ||
                    releasedMask != 0 ||
                    pulseMask != 0;
                const bool hasAnalog =
                    std::fabs(ParseFloat(frame[9])) > 0.0001f ||
                    std::fabs(ParseFloat(frame[10])) > 0.0001f ||
                    std::fabs(ParseFloat(frame[11])) > 0.0001f ||
                    std::fabs(ParseFloat(frame[12])) > 0.0001f ||
                    std::fabs(ParseFloat(frame[13])) > 0.0001f ||
                    std::fabs(ParseFloat(frame[14])) > 0.0001f;

                pollRows.push_back(CsvRow{
                    ToString(pollSequence),
                    context,
                    contextEpoch,
                    sourceTimestampUs,
                    ToString(unmanagedDownMask),
                    ToString(pressedMask),
                    ToString(releasedMask),
                    ToString(pulseMask),
                    ToString(unmanagedDownMask),
                    ToString(pressedMask),
                    ToString(releasedMask),
                    ToString(pulseMask),
                    "0",
                    "0",
                    "0",
                    "0",
                    frame[9],
                    frame[10],
                    frame[11],
                    frame[12],
                    frame[13],
                    frame[14],
                    ToString(hasDigital),
                    ToString(hasAnalog),
                    ToString(overflowed),
                    ToString(coalesced)
                });

                previousDownMask = downMask;
            }

            return pollRows;
        }

        ReplayResult CompareGeneratedBundle(
            const std::filesystem::path& scenarioPath,
            const std::filesystem::path& outputPath)
        {
            for (const auto& spec : Phase0TraceFiles()) {
                const auto expected = ReadCsvRows(scenarioPath / std::string(spec.name));
                const auto actual = ReadCsvRows(outputPath / std::string(spec.name));
                if (expected.size() != actual.size()) {
                    return Fail(
                        std::string(spec.name) +
                        ": behavioral row count mismatch expected=" + std::to_string(expected.size()) +
                        " actual=" + std::to_string(actual.size()));
                }
                for (std::size_t i = 0; i < expected.size(); ++i) {
                    if (expected[i] != actual[i]) {
                        return Fail(
                            std::string(spec.name) +
                            ": behavioral row mismatch at data row " + std::to_string(i + 1) +
                            " expected=" + JoinCsvRow(expected[i]) +
                            " actual=" + JoinCsvRow(actual[i]));
                    }
                }
            }

            return Pass("behavioral replay matched golden: " + outputPath.string());
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

        ReplayResult ReplayProcessorScenario(
            const std::filesystem::path& scenarioPath,
            const std::filesystem::path& outputPath)
        {
            try {
                CsvBundleRows bundle;
                auto processedFrames = ReadScenarioRows(scenarioPath, "processed_snapshot_frames.csv");
                auto processedEvents = ReadScenarioRows(scenarioPath, "processed_snapshot_events.csv");
                OutputRows(bundle, "processed_snapshot_frames.csv") = processedFrames;
                OutputRows(bundle, "processed_snapshot_events.csv") = processedEvents;
                OutputRows(bundle, "expected_authoritative_poll.csv") =
                    GeneratePollRows(processedFrames, processedEvents);

                WriteGeneratedBundle(outputPath, bundle);
                const auto comparison = CompareGeneratedBundle(scenarioPath, outputPath);
                if (!comparison.ok) {
                    return comparison;
                }
                return Pass("processor behavioral replay matched golden: " + outputPath.string());
            } catch (const std::exception& e) {
                return Fail(std::string("processor behavioral replay failed: ") + e.what());
            }
        }

        ReplayResult ReplayDispatcherScenario(
            const std::filesystem::path& scenarioPath,
            const std::filesystem::path& outputPath)
        {
            try {
                const auto schedule = ReadScenarioRows(scenarioPath, "dispatcher_schedule.csv");
                const auto ingressFrames = ReadScenarioRows(scenarioPath, "ingress_snapshot_frames.csv");
                const auto ingressEvents = ReadScenarioRows(scenarioPath, "ingress_snapshot_events.csv");
                const auto framesBySequence = FramesBySequence(ingressFrames);
                const auto eventsBySequence = EventsBySequence(ingressEvents);

                CsvBundleRows bundle;
                auto& actualSchedule = OutputRows(bundle, "dispatcher_schedule.csv");
                auto& actualIngressFrames = OutputRows(bundle, "ingress_snapshot_frames.csv");
                auto& actualIngressEvents = OutputRows(bundle, "ingress_snapshot_events.csv");
                auto& actualProcessedFrames = OutputRows(bundle, "processed_snapshot_frames.csv");
                auto& actualProcessedEvents = OutputRows(bundle, "processed_snapshot_events.csv");

                std::deque<std::uint64_t> pending;
                for (const auto& row : schedule) {
                    if (row.size() < 11) {
                        throw std::runtime_error("dispatcher schedule row has too few columns");
                    }

                    actualSchedule.push_back(row);
                    const auto& op = row[1];
                    const auto sequence = ParseU64(row[2], "sequence");
                    const auto pendingBefore = ParseSize(row[8], "pending_before");
                    const auto pendingAfter = ParseSize(row[9], "pending_after");
                    const auto drainedCount = ParseSize(row[10], "drained_count");
                    if (pendingBefore != pending.size()) {
                        throw std::runtime_error("dispatcher schedule pending_before mismatch at step " + row[0]);
                    }

                    if (op == "submit") {
                        const auto frame = framesBySequence.find(sequence);
                        if (frame == framesBySequence.end()) {
                            throw std::runtime_error("missing ingress frame for submitted sequence " + row[2]);
                        }
                        actualIngressFrames.push_back(frame->second);
                        AppendEventsForSequence(actualIngressEvents, eventsBySequence, sequence);
                        pending.push_back(sequence);
                    } else if (op == "drain") {
                        for (std::size_t i = 0; i < drainedCount; ++i) {
                            if (pending.empty()) {
                                throw std::runtime_error("dispatcher drain exceeded pending queue at step " + row[0]);
                            }
                            const auto drainedSequence = pending.front();
                            pending.pop_front();
                            const auto frame = framesBySequence.find(drainedSequence);
                            if (frame == framesBySequence.end()) {
                                throw std::runtime_error("missing ingress frame for drained sequence");
                            }
                            actualProcessedFrames.push_back(frame->second);
                            AppendEventsForSequence(actualProcessedEvents, eventsBySequence, drainedSequence);
                        }
                    } else {
                        throw std::runtime_error("unknown dispatcher schedule op: " + op);
                    }

                    if (pendingAfter != pending.size()) {
                        throw std::runtime_error("dispatcher schedule pending_after mismatch at step " + row[0]);
                    }
                }

                OutputRows(bundle, "expected_authoritative_poll.csv") =
                    GeneratePollRows(actualProcessedFrames, actualProcessedEvents);

                WriteGeneratedBundle(outputPath, bundle);
                const auto comparison = CompareGeneratedBundle(scenarioPath, outputPath);
                if (!comparison.ok) {
                    return comparison;
                }
                return Pass("dispatcher behavioral replay matched golden: " + outputPath.string());
            } catch (const std::exception& e) {
                return Fail(std::string("dispatcher behavioral replay failed: ") + e.what());
            }
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
            return ReplayDispatcherScenario(scenarioPath, outputPath);
        case ReplayMode::Processor:
            return ReplayProcessorScenario(scenarioPath, outputPath);
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
            return Pass("batch dispatcher behavioral replay matched scenarios=" + std::to_string(scenarios));
        case ReplayMode::Processor:
            return Pass("batch processor behavioral replay matched scenarios=" + std::to_string(scenarios));
        }

        return Fail("unknown replay mode");
    }
}
