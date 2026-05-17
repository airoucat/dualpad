#include "pch.h"

#include "input_v2/telemetry/ReplayHarness.h"
#include "input_v2/telemetry/TraceSchema.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace telemetry = dualpad::input_v2::telemetry;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    std::vector<std::string> ReadLines(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
        return lines;
    }

    void WriteFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::trunc);
        out << contents;
    }

    void WriteHeaderOnlyBundle(const std::filesystem::path& scenario)
    {
        std::filesystem::create_directories(scenario);
        for (const auto& spec : telemetry::Phase0TraceFiles()) {
            WriteFile(scenario / spec.name, std::string(spec.header) + "\n");
        }
    }

    std::filesystem::path ProjectRoot()
    {
        auto current = std::filesystem::current_path();
        while (!current.empty()) {
            if (std::filesystem::is_regular_file(current / "xmake.lua")) {
                return current;
            }
            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        throw std::runtime_error("failed to locate project root from current working directory");
    }

    std::filesystem::path TempRoot()
    {
        return ProjectRoot() / "build" / "replay-harness-tests";
    }

    void TestSchemaRoundTrip()
    {
        Require(telemetry::kTraceSchemaVersion == 1, "Phase 0 schema version should be fixed at 1");
        Require(telemetry::Phase0TraceFiles().size() == 10, "Phase 0 should expose ten fixed CSV files");

        const auto* dispatcher = telemetry::FindPhase0TraceFile("dispatcher_schedule.csv");
        Require(dispatcher != nullptr, "dispatcher schedule schema should exist");
        Require(
            dispatcher->header ==
                "step_index,op,sequence,budget,reason,route_state,last_poll_age_ms,hook_installed,pending_before,pending_after,drained_count",
            "dispatcher schedule header should remain stable");
    }

    void TestDispatcherScheduleReplayOrder()
    {
        const auto root = TempRoot() / "dispatcher";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteHeaderOnlyBundle(scenario);
        WriteFile(
            scenario / "dispatcher_schedule.csv",
            "step_index,op,sequence,budget,reason,route_state,last_poll_age_ms,hook_installed,pending_before,pending_after,drained_count\n"
            "0,submit,1,0,frame_pump_disabled,disabled,none,false,0,1,0\n"
            "1,drain,0,16,upstream_poll,active_fresh,4,true,1,0,1\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Dispatcher, actual);
        Require(result.ok, result.message);

        const auto lines = ReadLines(actual / "dispatcher_schedule.csv");
        Require(lines.size() == 3, "dispatcher replay should preserve header plus two schedule rows");
        Require(lines[1].starts_with("0,submit,1,"), "submit row should stay first");
        Require(lines[2].starts_with("1,drain,0,"), "drain row should stay second");
    }

    void TestProcessedPollRoundTrip()
    {
        const auto root = TempRoot() / "processor";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteHeaderOnlyBundle(scenario);
        WriteFile(
            scenario / "expected_authoritative_poll.csv",
            "poll_sequence,context,context_epoch,source_timestamp_us,down_mask,pressed_mask,released_mask,pulse_mask,unmanaged_down_mask,unmanaged_pressed_mask,unmanaged_released_mask,unmanaged_pulse_mask,managed_mask,committed_down_mask,committed_pressed_mask,committed_released_mask,move_x,move_y,look_x,look_y,left_trigger,right_trigger,has_digital,has_analog,overflowed,coalesced\n"
            "7,Gameplay,3,1234,2,2,0,0,0,0,0,0,2,2,2,0,0.25,-0.5,0,0,1,0,true,true,false,false\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Processor, actual);
        Require(result.ok, result.message);

        const auto lines = ReadLines(actual / "expected_authoritative_poll.csv");
        Require(lines.size() == 2, "processor replay should preserve authoritative poll rows");
        Require(lines[1].find("Gameplay,3,1234") != std::string::npos, "poll row should preserve context and timestamp");
    }

    void TestGlyphRoundTrip()
    {
        const auto root = TempRoot() / "glyph";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteHeaderOnlyBundle(scenario);
        WriteFile(
            scenario / "glyph_queries.csv",
            "query_id,sequence,action_id,context_name\n"
            "0,42,Menu.Confirm,Menu\n");
        WriteFile(
            scenario / "expected_glyph_results.csv",
            "query_id,ok,button_art_token,semantic_id,context_name\n"
            "0,true,360_A,Menu.Confirm,Menu\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Dispatcher, actual);
        Require(result.ok, result.message);

        const auto lines = ReadLines(actual / "expected_glyph_results.csv");
        Require(lines.size() == 2, "glyph replay should preserve expected results");
        Require(lines[1] == "0,true,360_A,Menu.Confirm,Menu", "glyph result shape should stay old compat descriptor fields");
    }

    void TestKeyboardCommandRoundTrip()
    {
        const auto root = TempRoot() / "keyboard";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteHeaderOnlyBundle(scenario);
        WriteFile(
            scenario / "expected_keyboard_bridge.csv",
            "sequence,command_index,command_type,scancode,action_id,contract,context\n"
            "9,0,pulse,100,ModEvent1,Pulse,Gameplay\n"
            "9,1,release,100,ModEvent1,Hold,Gameplay\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Dispatcher, actual);
        Require(result.ok, result.message);

        const auto lines = ReadLines(actual / "expected_keyboard_bridge.csv");
        Require(lines.size() == 3, "keyboard replay should preserve command sequence");
        Require(lines[1].find(",0,pulse,") != std::string::npos, "first keyboard command should stay first");
        Require(lines[2].find(",1,release,") != std::string::npos, "second keyboard command should stay second");
    }

    void MaterializePhase0GoldenForDiff()
    {
        const auto result = telemetry::ReplayBatch(
            ProjectRoot() / "tests" / "replay" / "golden" / "phase0",
            telemetry::ReplayMode::Dispatcher,
            ProjectRoot() / "build" / "replay");
        Require(result.ok, result.message);
    }
}

int main()
{
    TestSchemaRoundTrip();
    TestDispatcherScheduleReplayOrder();
    TestProcessedPollRoundTrip();
    TestGlyphRoundTrip();
    TestKeyboardCommandRoundTrip();
    MaterializePhase0GoldenForDiff();
    return 0;
}
