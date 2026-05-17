#include "pch.h"
#include "input_v2/telemetry/InputTraceRecorder.h"

#include "input/RuntimeConfig.h"
#include "input_v2/telemetry/TraceSchema.h"

#include <fstream>
#include <sstream>

namespace dualpad::input_v2::telemetry
{
    namespace
    {
        std::string BoolString(bool value)
        {
            return value ? "true" : "false";
        }

        template <class T>
        std::string ToScalarString(T value)
        {
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }

        std::string EscapeCsv(std::string_view value)
        {
            bool needsQuotes = false;
            for (const char ch : value) {
                if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
                    needsQuotes = true;
                    break;
                }
            }

            if (!needsQuotes) {
                return std::string(value);
            }

            std::string escaped;
            escaped.reserve(value.size() + 2);
            escaped.push_back('"');
            for (const char ch : value) {
                if (ch == '"') {
                    escaped.push_back('"');
                }
                escaped.push_back(ch);
            }
            escaped.push_back('"');
            return escaped;
        }

        std::string LastPollAgeString(const input::DrainTelemetryContext& telemetry)
        {
            return telemetry.lastPollAgeMs ? std::to_string(*telemetry.lastPollAgeMs) : "none";
        }

        std::string SnapshotFrameLine(const input::PadEventSnapshot& snapshot)
        {
            std::ostringstream line;
            line << snapshot.sequence << ','
                 << snapshot.firstSequence << ','
                 << snapshot.sourceTimestampUs << ','
                 << input::ToString(snapshot.context) << ','
                 << snapshot.contextEpoch << ','
                 << BoolString(snapshot.overflowed) << ','
                 << BoolString(snapshot.coalesced) << ','
                 << BoolString(snapshot.crossContextMismatch) << ','
                 << snapshot.state.buttons.digitalMask << ','
                 << snapshot.state.leftStick.x << ','
                 << snapshot.state.leftStick.y << ','
                 << snapshot.state.rightStick.x << ','
                 << snapshot.state.rightStick.y << ','
                 << snapshot.state.leftTrigger.normalized << ','
                 << snapshot.state.rightTrigger.normalized;
            return line.str();
        }

        std::string SnapshotEventLine(
            std::uint64_t sequence,
            std::size_t eventIndex,
            const input::PadEvent& event)
        {
            std::ostringstream line;
            line << sequence << ','
                 << eventIndex << ','
                 << input::ToString(event.type) << ','
                 << input::ToString(event.triggerType) << ','
                 << event.code << ','
                 << event.modifierMask << ','
                 << input::ToString(event.axis) << ','
                 << event.previousValue << ','
                 << event.value << ','
                 << event.timestampUs << ','
                 << static_cast<unsigned int>(event.touchId) << ','
                 << event.touchX << ','
                 << event.touchY << ','
                 << input::ToString(event.touchpadMode) << ','
                 << input::ToString(event.touchRegion) << ','
                 << input::ToString(event.slideDirection);
            return line.str();
        }

        std::string PollFrameLine(const input::AuthoritativePollFrame& frame)
        {
            std::ostringstream line;
            line << frame.pollSequence << ','
                 << input::ToString(frame.context) << ','
                 << frame.contextEpoch << ','
                 << frame.sourceTimestampUs << ','
                 << frame.downMask << ','
                 << frame.pressedMask << ','
                 << frame.releasedMask << ','
                 << frame.pulseMask << ','
                 << frame.unmanagedDownMask << ','
                 << frame.unmanagedPressedMask << ','
                 << frame.unmanagedReleasedMask << ','
                 << frame.unmanagedPulseMask << ','
                 << frame.managedMask << ','
                 << frame.committedDownMask << ','
                 << frame.committedPressedMask << ','
                 << frame.committedReleasedMask << ','
                 << frame.moveX << ','
                 << frame.moveY << ','
                 << frame.lookX << ','
                 << frame.lookY << ','
                 << frame.leftTrigger << ','
                 << frame.rightTrigger << ','
                 << BoolString(frame.hasDigital) << ','
                 << BoolString(frame.hasAnalog) << ','
                 << BoolString(frame.overflowed) << ','
                 << BoolString(frame.coalesced);
            return line.str();
        }

        const char* ToString(input::backend::KeyboardBridgeCommandType type)
        {
            switch (type) {
            case input::backend::KeyboardBridgeCommandType::Press:
                return "press";
            case input::backend::KeyboardBridgeCommandType::Release:
                return "release";
            case input::backend::KeyboardBridgeCommandType::Pulse:
                return "pulse";
            case input::backend::KeyboardBridgeCommandType::Reset:
                return "reset";
            case input::backend::KeyboardBridgeCommandType::None:
            default:
                return "none";
            }
        }
    }

    InputTraceRecorder& InputTraceRecorder::GetSingleton()
    {
        static InputTraceRecorder instance;
        return instance;
    }

    void InputTraceRecorder::ResetSession()
    {
        std::scoped_lock lock(_mutex);
        _activeRoot.clear();
        _activeSession.clear();
        _headersReady = false;
        _activeSnapshotSequence = 0;
        _scheduleStepIndex = 0;
        _keyboardCommandIndex = 0;
        _glyphQueryId = 0;
    }

    void InputTraceRecorder::SetActiveSnapshotSequence(std::uint64_t sequence)
    {
        std::scoped_lock lock(_mutex);
        _activeSnapshotSequence = sequence;
    }

    bool InputTraceRecorder::EnsureSessionLocked()
    {
        const auto& config = input::RuntimeConfig::GetSingleton();
        if (!config.EnableTraceRecording()) {
            return false;
        }

        const auto root = config.TraceOutputDir();
        const auto session = config.TraceSession();
        if (_activeRoot != root || _activeSession != session) {
            _activeRoot = root;
            _activeSession = std::string(session);
            _headersReady = false;
            _activeSnapshotSequence = 0;
            _scheduleStepIndex = 0;
            _keyboardCommandIndex = 0;
            _glyphQueryId = 0;
        }

        EnsureHeadersLocked();
        return true;
    }

    void InputTraceRecorder::EnsureHeadersLocked()
    {
        if (_headersReady) {
            return;
        }

        const auto directory = ResolveSessionDirectoryLocked();
        std::filesystem::create_directories(directory);
        for (const auto& spec : Phase0TraceFiles()) {
            const auto filePath = directory / spec.name;
            if (std::filesystem::exists(filePath) && std::filesystem::file_size(filePath) != 0) {
                continue;
            }

            std::ofstream out(filePath, std::ios::trunc);
            out << spec.header << '\n';
        }

        _headersReady = true;
    }

    std::filesystem::path InputTraceRecorder::ResolveSessionDirectoryLocked() const
    {
        return _activeRoot / _activeSession;
    }

    void InputTraceRecorder::AppendLineLocked(std::string_view fileName, std::string_view line)
    {
        const auto filePath = ResolveSessionDirectoryLocked() / fileName;
        std::ofstream out(filePath, std::ios::app);
        out << line << '\n';
    }

    void InputTraceRecorder::RecordDispatcherSubmit(
        const input::PadEventSnapshot& snapshot,
        std::size_t pendingBefore,
        std::size_t pendingAfter)
    {
        std::scoped_lock lock(_mutex);
        if (!EnsureSessionLocked()) {
            return;
        }

        std::ostringstream schedule;
        schedule << _scheduleStepIndex++ << ",submit,"
                 << snapshot.sequence
                 << ",0,frame_pump_disabled,disabled,none,false,"
                 << pendingBefore << ','
                 << pendingAfter
                 << ",0";
        AppendLineLocked("dispatcher_schedule.csv", schedule.str());
        AppendLineLocked("ingress_snapshot_frames.csv", SnapshotFrameLine(snapshot));
        for (std::size_t i = 0; i < snapshot.events.count; ++i) {
            AppendLineLocked("ingress_snapshot_events.csv", SnapshotEventLine(snapshot.sequence, i, snapshot.events[i]));
        }
    }

    void InputTraceRecorder::RecordDispatcherDrain(
        const input::DrainTelemetryContext& telemetry,
        std::size_t budget,
        std::size_t drained,
        std::size_t pendingBefore,
        std::size_t pendingAfter)
    {
        std::scoped_lock lock(_mutex);
        if (!EnsureSessionLocked()) {
            return;
        }

        std::ostringstream schedule;
        schedule << _scheduleStepIndex++ << ",drain,0,"
                 << budget << ','
                 << input::ToString(telemetry.reason) << ','
                 << input::ToString(telemetry.routeState) << ','
                 << LastPollAgeString(telemetry) << ','
                 << BoolString(telemetry.hookInstalled) << ','
                 << pendingBefore << ','
                 << pendingAfter << ','
                 << drained;
        AppendLineLocked("dispatcher_schedule.csv", schedule.str());
    }

    void InputTraceRecorder::RecordProcessedSnapshot(
        const input::PadEventSnapshot& snapshot,
        const input::AuthoritativePollFrame& pollFrame,
        const input::InputModalityTracker::ReplayCompatibilitySurface& presentationSurface)
    {
        std::scoped_lock lock(_mutex);
        if (!EnsureSessionLocked()) {
            return;
        }

        AppendLineLocked("processed_snapshot_frames.csv", SnapshotFrameLine(snapshot));
        for (std::size_t i = 0; i < snapshot.events.count; ++i) {
            AppendLineLocked("processed_snapshot_events.csv", SnapshotEventLine(snapshot.sequence, i, snapshot.events[i]));
        }
        AppendLineLocked("expected_authoritative_poll.csv", PollFrameLine(pollFrame));

        std::ostringstream presentation;
        presentation << snapshot.sequence << ','
                     << input::ToString(presentationSurface.context) << ','
                     << presentationSurface.contextEpoch << ','
                     << BoolString(presentationSurface.isUsingGamepad) << ','
                     << BoolString(presentationSurface.gamepadControlsCursor) << ','
                     << BoolString(presentationSurface.gamepadDeviceEnabled) << ','
                     << presentationSurface.presentationOwner << ','
                     << presentationSurface.cursorOwner << ','
                     << presentationSurface.gameplayEngineOwner << ','
                     << presentationSurface.gameplayMenuEntryOwner;
        AppendLineLocked("expected_presentation_surface.csv", presentation.str());
    }

    void InputTraceRecorder::RecordKeyboardCommand(
        input::backend::KeyboardBridgeCommandType type,
        std::uint8_t scancode,
        std::string_view actionId,
        input::backend::ActionOutputContract contract,
        input::InputContext context)
    {
        std::scoped_lock lock(_mutex);
        if (!EnsureSessionLocked()) {
            return;
        }

        std::ostringstream command;
        command << _activeSnapshotSequence << ','
                << _keyboardCommandIndex++ << ','
                << ToString(type) << ','
                << static_cast<unsigned int>(scancode) << ','
                << EscapeCsv(actionId) << ','
                << input::backend::ToString(contract) << ','
                << input::ToString(context);
        AppendLineLocked("expected_keyboard_bridge.csv", command.str());
    }

    void InputTraceRecorder::RecordGlyphResult(
        std::string_view actionId,
        std::string_view requestedContextName,
        const input::glyph::GlyphResolutionCompatResult& resolution)
    {
        std::scoped_lock lock(_mutex);
        if (!EnsureSessionLocked()) {
            return;
        }

        const auto queryId = _glyphQueryId++;
        std::ostringstream query;
        query << queryId << ",0,"
              << EscapeCsv(actionId) << ','
              << EscapeCsv(requestedContextName);
        AppendLineLocked("glyph_queries.csv", query.str());

        std::ostringstream result;
        result << queryId << ','
               << BoolString(resolution.ok) << ','
               << EscapeCsv(resolution.ok ? std::string_view(resolution.token) : std::string_view("")) << ','
               << EscapeCsv(actionId) << ','
               << EscapeCsv(requestedContextName);
        AppendLineLocked("expected_glyph_results.csv", result.str());
    }
}
