#include "pch.h"
#include "input_v2/telemetry/ReplayHarness.h"

#include <filesystem>
#include <iostream>
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
        dualpad::input_v2::telemetry::ReplayMode mode{ dualpad::input_v2::telemetry::ReplayMode::Dispatcher };
    };

    void PrintUsage()
    {
        std::cerr
            << "Usage:\n"
            << "  DualPadReplayHarness --scenario <dir> --mode dispatcher|processor --output <dir>\n"
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
}

int main(int argc, char** argv)
{
    const auto args = ParseArgs(argc, argv);
    dualpad::input_v2::telemetry::ReplayResult result{};

    if (!args.batch.empty()) {
        if (args.outputRoot.empty()) {
            PrintUsage();
            return 2;
        }
        result = dualpad::input_v2::telemetry::ReplayBatch(args.batch, args.mode, args.outputRoot);
    } else {
        if (args.scenario.empty() || args.output.empty()) {
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
