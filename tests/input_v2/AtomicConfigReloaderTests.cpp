#include "pch.h"

#include "input_v2/config/AtomicConfigReloader.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
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

    void WriteFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << contents;
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::string s;
        std::getline(in, s, '\0');
        return s;
    }
}

void RunAtomicConfigReloaderTests()
{
    namespace cfg = dualpad::input_v2::config;

    const auto root = FindProjectRoot(std::filesystem::current_path());
    const auto fixtureBindings = root / "tests" / "fixtures" / "input_v2" / "valid_bindings.ini";
    const auto fixturePolicy = root / "tests" / "fixtures" / "input_v2" / "valid_menu_policy.ini";

    const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-reloader";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    const auto bindings = temp / "DualPadBindings.ini";
    const auto policy = temp / "DualPadMenuPolicy.ini";

    WriteFile(bindings, ReadFile(fixtureBindings));
    WriteFile(policy, ReadFile(fixturePolicy));

    // Fresh startup compile + promote + disk LKG write.
    cfg::AtomicConfigReloader::GetSingleton().ResetForTests();
    {
        const auto res = cfg::AtomicConfigReloader::GetSingleton().LoadOrRecover(bindings, policy);
        Require(res.ok, res.message);
        const auto epoch = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epoch.has_value() && *epoch == 1, "first promote should set manifestEpoch=1");

        const auto lkg = temp / "DualPad.Manifest.lkg.json";
        Require(std::filesystem::exists(lkg), "disk LKG json should be written after promote");
    }

    // Runtime reload failure keeps existing active bundle.
    {
        WriteFile(bindings, "[Gameplay]\nButton:Cross=Game.NotARealAction\n");
        const auto res = cfg::AtomicConfigReloader::GetSingleton().Reload();
        Require(!res.ok, "reload with invalid config should fail");
        const auto epoch = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epoch.has_value() && *epoch == 1, "failed reload must keep active epoch");
    }

    // Startup recovery uses disk last-known-good when config is invalid and no active bundle exists.
    {
        cfg::AtomicConfigReloader::GetSingleton().ResetForTests();
        const auto res = cfg::AtomicConfigReloader::GetSingleton().LoadOrRecover(bindings, policy);
        Require(res.ok, "startup with invalid config should recover from disk LKG");
        Require(res.recoveredFromDiskLkg, "startup recovery should report recoveredFromDiskLkg=true");
        const auto epoch = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epoch.has_value() && *epoch == 1, "recovered epoch should match LKG epoch");
    }

    // Startup with invalid config and no disk LKG fails.
    {
        const auto temp2 = std::filesystem::temp_directory_path() / "dualpad-inputv2-reloader-no-lkg";
        std::filesystem::remove_all(temp2);
        std::filesystem::create_directories(temp2);

        const auto bindings2 = temp2 / "DualPadBindings.ini";
        const auto policy2 = temp2 / "DualPadMenuPolicy.ini";
        WriteFile(bindings2, "[Gameplay]\nButton:Cross=Game.NotARealAction\n");
        WriteFile(policy2, "[Policy]\nunknown_menu_policy=track\n");

        cfg::AtomicConfigReloader::GetSingleton().ResetForTests();
        const auto res = cfg::AtomicConfigReloader::GetSingleton().LoadOrRecover(bindings2, policy2);
        Require(!res.ok, "startup must fail when config is invalid and no LKG exists");
    }

    // Config files missing -> built-in defaults are allowed.
    {
        const auto temp3 = std::filesystem::temp_directory_path() / "dualpad-inputv2-reloader-missing";
        std::filesystem::remove_all(temp3);
        std::filesystem::create_directories(temp3);
        const auto bindings3 = temp3 / "DualPadBindings.ini";
        const auto policy3 = temp3 / "DualPadMenuPolicy.ini";

        cfg::AtomicConfigReloader::GetSingleton().ResetForTests();
        const auto res = cfg::AtomicConfigReloader::GetSingleton().LoadOrRecover(bindings3, policy3);
        Require(res.ok, "startup should succeed when config files are missing (built-in defaults)");
    }
}

