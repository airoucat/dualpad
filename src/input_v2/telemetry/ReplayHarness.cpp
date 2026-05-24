#include "pch.h"
#include "input_v2/telemetry/ReplayHarness.h"

#include "input/BindingConfig.h"
#include "input/InputContextNames.h"
#include "input/InputModalityTracker.h"
#include "input/RuntimeConfig.h"
#include "input/backend/KeyboardHelperBackend.h"
#include "input/glyph/ScaleformGlyphBridge.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/PadEventSnapshotProcessor.h"
#include "input_v2/telemetry/InputTraceRecorder.h"
#include "input_v2/telemetry/TraceSchema.h"

#include <cmath>
#include <fstream>
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

        bool ParseBool(const std::string& value, std::string_view field)
        {
            if (value == "true") {
                return true;
            }
            if (value == "false") {
                return false;
            }
            throw std::runtime_error("invalid bool field " + std::string(field) + ": " + value);
        }

        input::InputContext ParseContext(const std::string& value)
        {
            const auto parsed = input::ParseInputContextName(value);
            if (!parsed) {
                throw std::runtime_error("invalid context: " + value);
            }
            return *parsed;
        }

        input::PadEventType ParsePadEventType(const std::string& value)
        {
            if (value == "None") return input::PadEventType::None;
            if (value == "ButtonPress") return input::PadEventType::ButtonPress;
            if (value == "ButtonRelease") return input::PadEventType::ButtonRelease;
            if (value == "AxisChange") return input::PadEventType::AxisChange;
            if (value == "Layer") return input::PadEventType::Layer;
            if (value == "Combo") return input::PadEventType::Combo;
            if (value == "Hold") return input::PadEventType::Hold;
            if (value == "Tap") return input::PadEventType::Tap;
            if (value == "Gesture") return input::PadEventType::Gesture;
            if (value == "TouchpadPress") return input::PadEventType::TouchpadPress;
            if (value == "TouchpadRelease") return input::PadEventType::TouchpadRelease;
            if (value == "TouchpadSlide") return input::PadEventType::TouchpadSlide;
            throw std::runtime_error("invalid pad event type: " + value);
        }

        input::TriggerType ParseTriggerType(const std::string& value)
        {
            if (value == "Button") return input::TriggerType::Button;
            if (value == "Gesture") return input::TriggerType::Gesture;
            if (value == "Layer") return input::TriggerType::Layer;
            if (value == "Combo") return input::TriggerType::Combo;
            if (value == "Axis") return input::TriggerType::Axis;
            if (value == "Hold") return input::TriggerType::Hold;
            if (value == "Tap") return input::TriggerType::Tap;
            throw std::runtime_error("invalid trigger type: " + value);
        }

        input::PadAxisId ParsePadAxisId(const std::string& value)
        {
            if (value == "None") return input::PadAxisId::None;
            if (value == "LeftStickX") return input::PadAxisId::LeftStickX;
            if (value == "LeftStickY") return input::PadAxisId::LeftStickY;
            if (value == "RightStickX") return input::PadAxisId::RightStickX;
            if (value == "RightStickY") return input::PadAxisId::RightStickY;
            if (value == "LeftTrigger") return input::PadAxisId::LeftTrigger;
            if (value == "RightTrigger") return input::PadAxisId::RightTrigger;
            throw std::runtime_error("invalid pad axis: " + value);
        }

        input::TouchpadMode ParseTouchpadMode(const std::string& value)
        {
            if (value == "LeftCenterRight") return input::TouchpadMode::LeftCenterRight;
            if (value == "Edge") return input::TouchpadMode::Edge;
            if (value == "Whole") return input::TouchpadMode::Whole;
            if (value == "Disabled") return input::TouchpadMode::Disabled;
            throw std::runtime_error("invalid touchpad mode: " + value);
        }

        input::TouchpadPressRegion ParseTouchpadRegion(const std::string& value)
        {
            if (value == "None") return input::TouchpadPressRegion::None;
            if (value == "Left") return input::TouchpadPressRegion::Left;
            if (value == "Center") return input::TouchpadPressRegion::Center;
            if (value == "Right") return input::TouchpadPressRegion::Right;
            if (value == "TopEdge") return input::TouchpadPressRegion::TopEdge;
            if (value == "BottomEdge") return input::TouchpadPressRegion::BottomEdge;
            if (value == "LeftEdge") return input::TouchpadPressRegion::LeftEdge;
            if (value == "RightEdge") return input::TouchpadPressRegion::RightEdge;
            if (value == "Whole") return input::TouchpadPressRegion::Whole;
            throw std::runtime_error("invalid touchpad region: " + value);
        }

        input::TouchpadSlideDirection ParseTouchpadSlideDirection(const std::string& value)
        {
            if (value == "None") return input::TouchpadSlideDirection::None;
            if (value == "Up") return input::TouchpadSlideDirection::Up;
            if (value == "Down") return input::TouchpadSlideDirection::Down;
            if (value == "Left") return input::TouchpadSlideDirection::Left;
            if (value == "Right") return input::TouchpadSlideDirection::Right;
            throw std::runtime_error("invalid touchpad slide direction: " + value);
        }

        input::DrainReason ParseDrainReason(const std::string& value)
        {
            if (value == "frame_pump_disabled") return input::DrainReason::FramePumpDisabled;
            if (value == "frame_pump_assist_stale") return input::DrainReason::FramePumpAssistStale;
            if (value == "task_fallback_high_water") return input::DrainReason::TaskFallbackHighWater;
            if (value == "upstream_poll") return input::DrainReason::UpstreamPoll;
            throw std::runtime_error("invalid drain reason: " + value);
        }

        input::UpstreamRouteState ParseRouteState(const std::string& value)
        {
            if (value == "disabled") return input::UpstreamRouteState::Disabled;
            if (value == "active_fresh") return input::UpstreamRouteState::ActiveFresh;
            if (value == "active_stale") return input::UpstreamRouteState::ActiveStale;
            throw std::runtime_error("invalid route state: " + value);
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

        input::PadEvent ParseEventRow(const CsvRow& row)
        {
            if (row.size() < 16) {
                throw std::runtime_error("event row has too few columns");
            }

            input::PadEvent event{};
            event.type = ParsePadEventType(row[2]);
            event.triggerType = ParseTriggerType(row[3]);
            event.code = ParseU32(row[4], "code");
            event.modifierMask = ParseU32(row[5], "modifier_mask");
            event.axis = ParsePadAxisId(row[6]);
            event.previousValue = ParseFloat(row[7]);
            event.value = ParseFloat(row[8]);
            event.timestampUs = ParseU64(row[9], "timestamp_us");
            event.touchId = static_cast<std::uint8_t>(ParseU32(row[10], "touch_id"));
            event.touchX = static_cast<std::uint16_t>(ParseU32(row[11], "touch_x"));
            event.touchY = static_cast<std::uint16_t>(ParseU32(row[12], "touch_y"));
            event.touchpadMode = ParseTouchpadMode(row[13]);
            event.touchRegion = ParseTouchpadRegion(row[14]);
            event.slideDirection = ParseTouchpadSlideDirection(row[15]);
            if (event.type == input::PadEventType::AxisChange &&
                event.code == 0 &&
                event.axis != input::PadAxisId::None) {
                event.code = static_cast<std::uint32_t>(event.axis);
            }
            return event;
        }

        input::PadEventSnapshot BuildSnapshot(const CsvRow& frame, const RowsBySequence& eventsBySequence)
        {
            if (frame.size() < 15) {
                throw std::runtime_error("snapshot frame row has too few columns");
            }

            input::PadEventSnapshot snapshot{};
            snapshot.sequence = ParseU64(frame[0], "sequence");
            snapshot.firstSequence = ParseU64(frame[1], "first_sequence");
            snapshot.sourceTimestampUs = ParseU64(frame[2], "source_timestamp_us");
            snapshot.context = ParseContext(frame[3]);
            snapshot.contextEpoch = ParseU32(frame[4], "context_epoch");
            snapshot.overflowed = ParseBool(frame[5], "overflowed");
            snapshot.coalesced = ParseBool(frame[6], "coalesced");
            snapshot.crossContextMismatch = ParseBool(frame[7], "cross_context_mismatch");
            snapshot.state.connected = true;
            snapshot.state.sequence = snapshot.sequence;
            snapshot.state.timestampUs = snapshot.sourceTimestampUs;
            snapshot.state.buttons.digitalMask = ParseU32(frame[8], "digital_mask");
            snapshot.state.leftStick.x = ParseFloat(frame[9]);
            snapshot.state.leftStick.y = ParseFloat(frame[10]);
            snapshot.state.rightStick.x = ParseFloat(frame[11]);
            snapshot.state.rightStick.y = ParseFloat(frame[12]);
            snapshot.state.leftTrigger.normalized = ParseFloat(frame[13]);
            snapshot.state.rightTrigger.normalized = ParseFloat(frame[14]);

            const auto events = eventsBySequence.find(snapshot.sequence);
            if (events != eventsBySequence.end()) {
                for (const auto& row : events->second) {
                    if (!snapshot.events.Push(ParseEventRow(row))) {
                        throw std::runtime_error("snapshot event buffer overflow while replaying sequence " +
                            ToString(snapshot.sequence));
                    }
                }
            }

            return snapshot;
        }

        input::DrainTelemetryContext BuildDrainTelemetry(const CsvRow& row)
        {
            if (row.size() < 11) {
                throw std::runtime_error("dispatcher schedule row has too few columns");
            }

            std::optional<std::uint64_t> lastPollAgeMs{};
            if (row[6] != "none") {
                lastPollAgeMs = ParseU64(row[6], "last_poll_age_ms");
            }

            return input::DrainTelemetryContext{
                .reason = ParseDrainReason(row[4]),
                .routeState = ParseRouteState(row[5]),
                .lastPollAgeMs = lastPollAgeMs,
                .hookInstalled = ParseBool(row[7], "hook_installed")
            };
        }

        std::filesystem::path FindProjectRoot(std::filesystem::path from)
        {
            if (std::filesystem::is_regular_file(from)) {
                from = from.parent_path();
            }
            while (!from.empty()) {
                if (std::filesystem::is_regular_file(from / "xmake.lua")) {
                    return from;
                }
                const auto parent = from.parent_path();
                if (parent == from) {
                    break;
                }
                from = parent;
            }
            return std::filesystem::current_path();
        }

        void LoadReplayRuntimeConfig(const std::filesystem::path& scenarioPath)
        {
            const auto projectRoot = FindProjectRoot(scenarioPath);
            input::RuntimeConfig::GetSingleton().Load(projectRoot / "config" / "DualPadDebug.ini");
            input::BindingConfig::GetSingleton().Load(projectRoot / "config" / "DualPadBindings.ini");
        }

        struct ReplayRuntimeSession
        {
            explicit ReplayRuntimeSession(std::filesystem::path outputPath) :
                output(std::move(outputPath))
            {
                std::filesystem::remove_all(output);
                input_v2::telemetry::InputTraceRecorder::GetSingleton().BeginReplaySession(
                    output.parent_path(),
                    output.filename().string());
                input::backend::KeyboardHelperBackend::GetSingleton().SetReplayRouteActive(true);
            }

            ~ReplayRuntimeSession()
            {
                Finish();
            }

            void Finish()
            {
                if (!active) {
                    return;
                }
                input::backend::KeyboardHelperBackend::GetSingleton().SetReplayRouteActive(false);
                input_v2::telemetry::InputTraceRecorder::GetSingleton().EndReplaySession();
                active = false;
            }

            std::filesystem::path output;
            bool active{ true };
        };

        void ResetProcessorRuntimeForReplay()
        {
            input::backend::KeyboardHelperBackend::GetSingleton().SetReplayRouteActive(false);
            input::InputModalityTracker::GetSingleton().ResetForReplayCapture();
            input::PadEventSnapshotProcessor::GetSingleton().ResetState();
            input::backend::KeyboardHelperBackend::GetSingleton().SetReplayRouteActive(true);
        }

        void ProcessSnapshotThroughRuntime(const input::PadEventSnapshot& snapshot)
        {
            input::InputModalityTracker::GetSingleton().SetReplayContext(snapshot.context, snapshot.contextEpoch);
            input_v2::telemetry::InputTraceRecorder::GetSingleton().SetActiveSnapshotSequence(snapshot.sequence);
            input::PadEventSnapshotProcessor::GetSingleton().Process(snapshot);
        }

        void ProcessSnapshotThroughRuntimeSink(const input::PadEventSnapshot& snapshot, void*)
        {
            ProcessSnapshotThroughRuntime(snapshot);
        }

        void ReplayGlyphQueries(const std::filesystem::path& scenarioPath)
        {
            const auto glyphQueries = ReadScenarioRows(scenarioPath, "glyph_queries.csv");
            for (const auto& row : glyphQueries) {
                if (row.size() < 4) {
                    throw std::runtime_error("glyph query row has too few columns");
                }

                input_v2::telemetry::InputTraceRecorder::GetSingleton().SetActiveSnapshotSequence(
                    ParseU64(row[1], "glyph sequence"));
                input::InputModalityTracker::GetSingleton().SetReplayContext(ParseContext(row[3]), 0);
                (void)input::glyph::ScaleformGlyphBridge::GetSingleton().ReplayResolveActionGlyph(row[2], row[3]);
            }
        }

        void CarryProcessorInputRows(
            const std::filesystem::path& scenarioPath,
            const std::filesystem::path& outputPath)
        {
            for (const auto fileName : {
                    std::string_view("dispatcher_schedule.csv"),
                    std::string_view("ingress_snapshot_frames.csv"),
                    std::string_view("ingress_snapshot_events.csv") }) {
                const auto* spec = FindPhase0TraceFile(fileName);
                if (!spec) {
                    throw std::runtime_error("missing phase0 spec for processor input carry");
                }
                WriteCsvRows(outputPath, *spec, ReadScenarioRows(scenarioPath, fileName));
            }
        }

        // Legacy CSV simulator retained as a fixture-test helper only. Dispatcher
        // and processor replay modes must use the runtime seams below.
        [[maybe_unused]] CsvRows GenerateSyntheticPollRowsForFixtureTests(
            const CsvRows& processedFrames,
            const CsvRows& processedEvents)
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
                        ": runtime row count mismatch expected=" + std::to_string(expected.size()) +
                        " actual=" + std::to_string(actual.size()));
                }
                for (std::size_t i = 0; i < expected.size(); ++i) {
                    if (expected[i] != actual[i]) {
                        return Fail(
                            std::string(spec.name) +
                            ": runtime row mismatch at data row " + std::to_string(i + 1) +
                            " expected=" + JoinCsvRow(expected[i]) +
                            " actual=" + JoinCsvRow(actual[i]));
                    }
                }
            }

            return Pass("runtime replay matched golden: " + outputPath.string());
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
                LoadReplayRuntimeConfig(scenarioPath);
                ResetProcessorRuntimeForReplay();
                ReplayRuntimeSession session(outputPath);

                const auto processedFrames = ReadScenarioRows(scenarioPath, "processed_snapshot_frames.csv");
                const auto processedEvents = ReadScenarioRows(scenarioPath, "processed_snapshot_events.csv");
                const auto eventsBySequence = EventsBySequence(processedEvents);
                for (const auto& frame : processedFrames) {
                    ProcessSnapshotThroughRuntime(BuildSnapshot(frame, eventsBySequence));
                }

                ReplayGlyphQueries(scenarioPath);
                session.Finish();
                CarryProcessorInputRows(scenarioPath, outputPath);

                const auto comparison = CompareGeneratedBundle(scenarioPath, outputPath);
                if (!comparison.ok) {
                    return comparison;
                }
                return Pass("processor runtime replay matched golden: " + outputPath.string());
            } catch (const std::exception& e) {
                return Fail(std::string("processor runtime replay failed: ") + e.what());
            }
        }

        ReplayResult ReplayDispatcherScenario(
            const std::filesystem::path& scenarioPath,
            const std::filesystem::path& outputPath)
        {
            try {
                LoadReplayRuntimeConfig(scenarioPath);
                input::PadEventSnapshotDispatcher::GetSingleton().ResetForReplay();
                ResetProcessorRuntimeForReplay();
                ReplayRuntimeSession session(outputPath);

                const auto schedule = ReadScenarioRows(scenarioPath, "dispatcher_schedule.csv");
                const auto ingressFrames = ReadScenarioRows(scenarioPath, "ingress_snapshot_frames.csv");
                const auto ingressEvents = ReadScenarioRows(scenarioPath, "ingress_snapshot_events.csv");
                const auto framesBySequence = FramesBySequence(ingressFrames);
                const auto eventsBySequence = EventsBySequence(ingressEvents);
                for (const auto& row : schedule) {
                    if (row.size() < 11) {
                        throw std::runtime_error("dispatcher schedule row has too few columns");
                    }

                    const auto& op = row[1];
                    const auto sequence = ParseU64(row[2], "sequence");
                    if (op == "submit") {
                        const auto frame = framesBySequence.find(sequence);
                        if (frame == framesBySequence.end()) {
                            throw std::runtime_error("missing ingress frame for submitted sequence " + row[2]);
                        }
                        input::PadEventSnapshotDispatcher::GetSingleton().SubmitSnapshot(
                            BuildSnapshot(frame->second, eventsBySequence));
                    } else if (op == "drain") {
                        const auto telemetry = BuildDrainTelemetry(row);
                        (void)input::PadEventSnapshotDispatcher::GetSingleton().DrainForReplay(
                            ParseSize(row[3], "budget"),
                            &telemetry,
                            ProcessSnapshotThroughRuntimeSink,
                            nullptr);
                    } else {
                        throw std::runtime_error("unknown dispatcher schedule op: " + op);
                    }
                }

                ReplayGlyphQueries(scenarioPath);
                session.Finish();

                const auto comparison = CompareGeneratedBundle(scenarioPath, outputPath);
                if (!comparison.ok) {
                    return comparison;
                }
                return Pass("dispatcher runtime replay matched golden: " + outputPath.string());
            } catch (const std::exception& e) {
                return Fail(std::string("dispatcher runtime replay failed: ") + e.what());
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
            return Pass("batch dispatcher runtime replay matched scenarios=" + std::to_string(scenarios));
        case ReplayMode::Processor:
            return Pass("batch processor runtime replay matched scenarios=" + std::to_string(scenarios));
        }

        return Fail("unknown replay mode");
    }
}
