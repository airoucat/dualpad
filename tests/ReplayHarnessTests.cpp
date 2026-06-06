#include "pch.h"

#include "input_v2/telemetry/ReplayHarness.h"
#include "input_v2/telemetry/TraceSchema.h"

#include <filesystem>
#include <fstream>
#include <iostream>
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

    void WriteRuntimeReplayBundle(
        const std::filesystem::path& scenario,
        std::string_view dispatcherRows,
        std::string_view frameRows,
        std::string_view eventRows,
        std::string_view expectedPollRows)
    {
        WriteHeaderOnlyBundle(scenario);
        WriteFile(
            scenario / "dispatcher_schedule.csv",
            std::string(telemetry::FindPhase0TraceFile("dispatcher_schedule.csv")->header) + "\n" +
                std::string(dispatcherRows));
        WriteFile(
            scenario / "ingress_snapshot_frames.csv",
            std::string(telemetry::FindPhase0TraceFile("ingress_snapshot_frames.csv")->header) + "\n" +
                std::string(frameRows));
        WriteFile(
            scenario / "ingress_snapshot_events.csv",
            std::string(telemetry::FindPhase0TraceFile("ingress_snapshot_events.csv")->header) + "\n" +
                std::string(eventRows));
        WriteFile(
            scenario / "processed_snapshot_frames.csv",
            std::string(telemetry::FindPhase0TraceFile("processed_snapshot_frames.csv")->header) + "\n" +
                std::string(frameRows));
        WriteFile(
            scenario / "processed_snapshot_events.csv",
            std::string(telemetry::FindPhase0TraceFile("processed_snapshot_events.csv")->header) + "\n" +
                std::string(eventRows));
        WriteFile(
            scenario / "expected_authoritative_poll.csv",
            std::string(telemetry::FindPhase0TraceFile("expected_authoritative_poll.csv")->header) + "\n" +
                std::string(expectedPollRows));
    }

    void WriteProcessorBehaviorBundle(
        const std::filesystem::path& scenario,
        std::string_view frameRows,
        std::string_view eventRows,
        std::string_view expectedPollRows)
    {
        WriteHeaderOnlyBundle(scenario);
        WriteFile(
            scenario / "processed_snapshot_frames.csv",
            std::string(telemetry::FindPhase0TraceFile("processed_snapshot_frames.csv")->header) + "\n" +
                std::string(frameRows));
        WriteFile(
            scenario / "processed_snapshot_events.csv",
            std::string(telemetry::FindPhase0TraceFile("processed_snapshot_events.csv")->header) + "\n" +
                std::string(eventRows));
        WriteFile(
            scenario / "expected_authoritative_poll.csv",
            std::string(telemetry::FindPhase0TraceFile("expected_authoritative_poll.csv")->header) + "\n" +
                std::string(expectedPollRows));
    }

    void WritePresentationRows(
        const std::filesystem::path& scenario,
        std::string_view expectedPresentationRows)
    {
        WriteFile(
            scenario / "expected_presentation_surface.csv",
            std::string(telemetry::FindPhase0TraceFile("expected_presentation_surface.csv")->header) + "\n" +
                std::string(expectedPresentationRows));
    }

    void WriteRuntimeSurfaceBundle(
        const std::filesystem::path& scenario,
        std::string_view frameRows,
        std::string_view eventRows,
        std::string_view expectedPollRows,
        std::string_view expectedKeyboardRows,
        std::string_view expectedPresentationRows,
        std::string_view glyphQueryRows,
        std::string_view expectedGlyphRows)
    {
        WriteProcessorBehaviorBundle(scenario, frameRows, eventRows, expectedPollRows);
        WriteFile(
            scenario / "expected_keyboard_bridge.csv",
            std::string(telemetry::FindPhase0TraceFile("expected_keyboard_bridge.csv")->header) + "\n" +
                std::string(expectedKeyboardRows));
        WriteFile(
            scenario / "expected_presentation_surface.csv",
            std::string(telemetry::FindPhase0TraceFile("expected_presentation_surface.csv")->header) + "\n" +
                std::string(expectedPresentationRows));
        WriteFile(
            scenario / "glyph_queries.csv",
            std::string(telemetry::FindPhase0TraceFile("glyph_queries.csv")->header) + "\n" +
                std::string(glyphQueryRows));
        WriteFile(
            scenario / "expected_glyph_results.csv",
            std::string(telemetry::FindPhase0TraceFile("expected_glyph_results.csv")->header) + "\n" +
                std::string(expectedGlyphRows));
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

    void TestMaterializeFixturePreservesDispatcherScheduleRows()
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

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::MaterializeFixture, actual);
        Require(result.ok, result.message);

        const auto lines = ReadLines(actual / "dispatcher_schedule.csv");
        Require(lines.size() == 3, "fixture materialization should preserve header plus two schedule rows");
        Require(lines[1].starts_with("0,submit,1,"), "submit row should stay first");
        Require(lines[2].starts_with("1,drain,0,"), "drain row should stay second");
    }

    void TestMaterializeFixturePreservesProcessedPollRows()
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

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::MaterializeFixture, actual);
        Require(result.ok, result.message);

        const auto lines = ReadLines(actual / "expected_authoritative_poll.csv");
        Require(lines.size() == 2, "fixture materialization should preserve authoritative poll rows");
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

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::MaterializeFixture, actual);
        Require(result.ok, result.message);

        const auto lines = ReadLines(actual / "expected_glyph_results.csv");
        Require(lines.size() == 2, "glyph fixture materialization should preserve expected results");
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

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::MaterializeFixture, actual);
        Require(result.ok, result.message);

        const auto lines = ReadLines(actual / "expected_keyboard_bridge.csv");
        Require(lines.size() == 3, "keyboard fixture materialization should preserve command sequence");
        Require(lines[1].find(",0,pulse,") != std::string::npos, "first keyboard command should stay first");
        Require(lines[2].find(",1,release,") != std::string::npos, "second keyboard command should stay second");
    }

    void TestValidateSchemaDoesNotMaterializeOutput()
    {
        const auto root = TempRoot() / "validate-schema";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteHeaderOnlyBundle(scenario);

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::ValidateSchema, actual);
        Require(result.ok, result.message);
        Require(
            !std::filesystem::exists(actual / "dispatcher_schedule.csv"),
            "validate-schema mode must not materialize candidate output");
    }

    void TestDispatcherModeProducesCandidateOutput()
    {
        const auto root = TempRoot() / "dispatcher-behavior";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteRuntimeReplayBundle(
            scenario,
            "0,submit,1,0,frame_pump_disabled,disabled,none,false,0,1,0\n"
            "1,drain,0,1,upstream_poll,active_fresh,4,true,1,0,1\n",
            "1,1,1000,Gameplay,1,false,false,false,2,0.5,0,0,0,0,0\n",
            "1,0,ButtonPress,Button,2,0,None,0,0,1000,0,0,0,Disabled,None,None\n",
            "0,Gameplay,0,1000,0,0,0,0,0,0,0,0,0,0,0,0,0.5,0,0,0,0,0,true,true,false,false\n");
        WritePresentationRows(
            scenario,
            "1,Gameplay,1,false,false,false,KeyboardMouse,KeyboardMouse,Gamepad,Gamepad\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Dispatcher, actual);
        Require(result.ok, result.message);

        const auto processed = ReadLines(actual / "processed_snapshot_frames.csv");
        Require(processed.size() == 2, "dispatcher mode should produce processed snapshot candidate rows");
        Require(processed[1].starts_with("1,1,1000,Gameplay,"), "dispatcher mode should drain submitted ingress snapshot");
    }

    void TestDispatcherModeFailsOnBehavioralMismatch()
    {
        const auto root = TempRoot() / "dispatcher-mismatch";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteRuntimeReplayBundle(
            scenario,
            "0,submit,1,0,frame_pump_disabled,disabled,none,false,0,1,0\n"
            "1,drain,0,1,upstream_poll,active_fresh,4,true,1,0,1\n",
            "1,1,1000,Gameplay,1,false,false,false,2,0.5,0,0,0,0,0\n",
            "1,0,ButtonPress,Button,2,0,None,0,0,1000,0,0,0,Disabled,None,None\n",
            "1,Gameplay,1,1000,2,2,0,0,2,2,0,0,0,0,0,0,0.5,0,0,0,0,0,true,true,false,false\n");
        WriteFile(
            scenario / "processed_snapshot_frames.csv",
            std::string(telemetry::FindPhase0TraceFile("processed_snapshot_frames.csv")->header) + "\n"
            "1,1,1000,Gameplay,1,false,false,false,8,0.5,0,0,0,0,0\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Dispatcher, actual);
        Require(!result.ok, "dispatcher mode should fail when generated behavior differs from golden");
        const auto processed = ReadLines(actual / "processed_snapshot_frames.csv");
        Require(processed.size() == 2, "dispatcher mismatch should still leave generated candidate output");
        Require(
            processed[1].find(",2,0.5,") != std::string::npos,
            "dispatcher candidate output should come from ingress data, not copied processed golden");
    }

    void TestProcessorModeProducesCandidateOutput()
    {
        const auto root = TempRoot() / "processor-behavior";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteProcessorBehaviorBundle(
            scenario,
            "3,3,3000,Gameplay,2,false,false,false,2,0,0,0.25,0,0,1\n",
            "3,0,ButtonPress,Button,2,0,None,0,0,3000,0,0,0,Disabled,None,None\n",
            "0,Gameplay,0,3000,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.25,0,0,1,true,true,false,false\n");
        WritePresentationRows(
            scenario,
            "3,Gameplay,2,false,false,false,KeyboardMouse,KeyboardMouse,Gamepad,Gamepad\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Processor, actual);
        Require(result.ok, result.message);

        const auto poll = ReadLines(actual / "expected_authoritative_poll.csv");
        Require(poll.size() == 2, "processor mode should produce authoritative poll candidate rows");
        Require(poll[1].find("Gameplay,0,3000,0,0") != std::string::npos, "processor mode should reduce processed snapshots into runtime poll rows");
    }

    void TestProcessorModeFailsOnBehavioralMismatch()
    {
        const auto root = TempRoot() / "processor-mismatch";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteProcessorBehaviorBundle(
            scenario,
            "3,3,3000,Gameplay,2,false,false,false,4,0,0,0.25,0,0,1\n",
            "3,0,ButtonPress,Button,4,0,None,0,0,3000,0,0,0,Disabled,None,None\n",
            "1,Gameplay,2,3000,8,8,0,0,8,8,0,0,0,0,0,0,0,0,0.25,0,0,1,true,true,false,false\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Processor, actual);
        Require(!result.ok, "processor mode should fail when generated poll output differs from golden");
        const auto poll = ReadLines(actual / "expected_authoritative_poll.csv");
        Require(poll.size() == 2, "processor mismatch should still leave generated candidate output");
        Require(
            poll[1].find("Gameplay,0,3000,0,0") != std::string::npos,
            "processor candidate output should be generated from processed snapshots, not copied expected poll");
    }

    void TestProcessorModeCapturesRuntimeSurfaces()
    {
        const auto root = TempRoot() / "processor-runtime-surfaces";
        const auto scenario = root / "expected";
        const auto actual = root / "actual";
        std::filesystem::remove_all(root);
        WriteRuntimeSurfaceBundle(
            scenario,
            "9,9,9000,Gameplay,2,false,false,false,4,0,0,0,0,0,0\n",
            "9,0,ButtonPress,Button,4,0,None,0,0,9000,0,0,0,Disabled,None,None\n",
            "0,Gameplay,0,9000,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,true,true,false,false\n",
            "9,0,pulse,100,VirtualKey.DIK_F13,Pulse,Gameplay\n",
            "9,Gameplay,2,false,false,false,KeyboardMouse,KeyboardMouse,Gamepad,Gamepad\n",
            "0,9,Menu.Confirm,Menu\n",
            "0,true,360_Y,Menu.Confirm,Menu\n");

        const auto result = telemetry::ReplayScenario(scenario, telemetry::ReplayMode::Processor, actual);
        Require(result.ok, result.message);

        const auto keyboard = ReadLines(actual / "expected_keyboard_bridge.csv");
        Require(keyboard.size() == 2, "processor runtime replay should capture keyboard bridge commands");
        const auto presentation = ReadLines(actual / "expected_presentation_surface.csv");
        Require(presentation.size() == 2, "processor runtime replay should capture presentation surface rows");
        const auto glyph = ReadLines(actual / "expected_glyph_results.csv");
        Require(glyph.size() == 2, "processor runtime replay should capture glyph bridge results");
    }

    void TestPhase0SyntheticScenarioProducesRows()
    {
        const auto golden = ProjectRoot() / "tests" / "replay" / "golden" / "phase0" / "10_backlog_gap_overflow" /
            "dispatcher_schedule.csv";
        const auto actual = ProjectRoot() / "build" / "replay" / "10_backlog_gap_overflow" /
            "dispatcher_schedule.csv";

        const auto goldenLines = ReadLines(golden);
        Require(goldenLines.size() > 1, "10_backlog_gap_overflow must include at least one synthetic schedule row");

        const auto actualLines = ReadLines(actual);
        Require(actualLines.size() == goldenLines.size(), "runtime 10_backlog_gap_overflow output must keep every schedule row");
        Require(
            actualLines[1] == goldenLines[1],
            "runtime 10_backlog_gap_overflow output must keep the first synthetic schedule row");

        const auto configActual = ReadLines(
            ProjectRoot() / "build" / "replay" / "11_config_reload_success_failure" / "expected_authoritative_poll.csv");
        Require(configActual.size() > 1, "11_config_reload_success_failure must produce non-empty runtime poll rows");
    }

    void GeneratePhase0RuntimeReplayForDiff()
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
    try {
        TestSchemaRoundTrip();
        TestMaterializeFixturePreservesDispatcherScheduleRows();
        TestMaterializeFixturePreservesProcessedPollRows();
        TestGlyphRoundTrip();
        TestKeyboardCommandRoundTrip();
        TestValidateSchemaDoesNotMaterializeOutput();
        TestDispatcherModeProducesCandidateOutput();
        TestDispatcherModeFailsOnBehavioralMismatch();
        TestProcessorModeProducesCandidateOutput();
        TestProcessorModeFailsOnBehavioralMismatch();
        TestProcessorModeCapturesRuntimeSurfaces();
        GeneratePhase0RuntimeReplayForDiff();
        TestPhase0SyntheticScenarioProducesRows();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
