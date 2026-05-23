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
            if (key == "--") {
                continue;
            }

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

    std::filesystem::path FindProjectRoot()
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
        return {};
    }

    std::filesystem::path ResolveProjectRelative(
        const std::filesystem::path& path,
        const std::filesystem::path& projectRoot)
    {
        if (path.empty() || path.is_absolute() || projectRoot.empty()) {
            return path;
        }
        return projectRoot / path;
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

    const auto projectRoot = FindProjectRoot();
    args.scenario = ResolveProjectRelative(args.scenario, projectRoot);
    args.output = ResolveProjectRelative(args.output, projectRoot);
    args.batch = ResolveProjectRelative(args.batch, projectRoot);
    args.outputRoot = ResolveProjectRelative(args.outputRoot, projectRoot);

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
