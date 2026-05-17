#include "pch.h"
#include "input_v2/telemetry/ReplayHarness.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    struct Args
    {
        std::filesystem::path scenario;
        std::filesystem::path output;
        std::filesystem::path batch;
        std::filesystem::path outputRoot;
        dualpad::input_v2::telemetry::ReplayMode mode{ dualpad::input_v2::telemetry::ReplayMode::ValidateSchema };
    };

    void PrintUsage()
    {
        std::cerr
            << "Usage:\n"
            << "  DualPadReplayHarness --scenario <dir> --mode validate-schema\n"
            << "  DualPadReplayHarness --scenario <dir> --mode materialize-fixture --output <dir>\n"
            << "  DualPadReplayHarness --scenario <dir> --mode dispatcher|processor --output <dir>\n"
            << "  DualPadReplayHarness --batch <phase0-root> --mode validate-schema\n"
            << "  DualPadReplayHarness --batch <phase0-root> --mode materialize-fixture --output-root <dir>\n"
            << "  DualPadReplayHarness --batch <phase0-root> --mode dispatcher|processor --output-root <dir>\n";
    }

    Args ParseArgs(int argc, char** argv)
    {
        Args args{};
        for (int i = 1; i < argc; ++i) {
            const std::string_view key(argv[i]);
            const auto next = [&]() -> const char* {
                if (i + 1 >= argc) {
                    return "";
                }
                return argv[++i];
            };

            if (key == "--scenario") {
                args.scenario = next();
            } else if (key == "--output") {
                args.output = next();
            } else if (key == "--batch") {
                args.batch = next();
            } else if (key == "--output-root") {
                args.outputRoot = next();
            } else if (key == "--mode") {
                args.mode = dualpad::input_v2::telemetry::ParseReplayMode(next());
            }
        }
        return args;
    }

    bool RequiresOutput(dualpad::input_v2::telemetry::ReplayMode mode)
    {
        return mode != dualpad::input_v2::telemetry::ReplayMode::ValidateSchema;
    }
}

int main(int argc, char** argv)
{
    Args args{};
    try {
        args = ParseArgs(argc, argv);
    } catch (const std::invalid_argument& e) {
        std::cerr << e.what() << '\n';
        PrintUsage();
        return 2;
    }

    dualpad::input_v2::telemetry::ReplayResult result{};

    if (!args.batch.empty()) {
        if (RequiresOutput(args.mode) && args.outputRoot.empty()) {
            PrintUsage();
            return 2;
        }
        result = dualpad::input_v2::telemetry::ReplayBatch(args.batch, args.mode, args.outputRoot);
    } else {
        if (args.scenario.empty() || (RequiresOutput(args.mode) && args.output.empty())) {
            PrintUsage();
            return 2;
        }
        result = dualpad::input_v2::telemetry::ReplayScenario(args.scenario, args.mode, args.output);
    }

    if (!result.ok) {
        std::cerr << result.message << '\n';
        return 1;
    }

    std::cout << result.message << '\n';
    return 0;
}
