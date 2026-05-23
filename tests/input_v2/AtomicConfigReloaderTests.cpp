#include "pch.h"

#include "input/BindingConfig.h"
#include "input/MenuContextPolicy.h"
#include "input_v2/config/ActionManifestPublisher.h"
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
    cfg::ActionManifestPublisher::GetSingleton().ResetForTests();
    {
        const auto res = cfg::AtomicConfigReloader::GetSingleton().LoadOrRecover(bindings, policy);
        Require(res.ok, res.message);
        const auto epoch = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epoch.has_value() && *epoch == 1, "first promote should set manifestEpoch=1");
        Require(
            cfg::ActionManifestPublisher::GetSingleton().GetLastPublishedEpoch().value_or(0) == 1,
            "publisher should publish first promoted epoch");
        Require(
            !cfg::ActionManifestPublisher::GetSingleton().GetActiveEpochObservedAtLastPublish().has_value(),
            "first publish seam should run before active bundle is observable");

        const auto lkg = temp / "DualPad.Manifest.lkg.json";
        Require(std::filesystem::exists(lkg), "disk LKG json should be written after promote");
    }

    // Second successful promote publishes before swapping active to the new epoch.
    {
        WriteFile(bindings, ReadFile(fixtureBindings));
        const auto res = cfg::AtomicConfigReloader::GetSingleton().Reload();
        Require(res.ok, res.message);
        Require(
            cfg::ActionManifestPublisher::GetSingleton().GetLastPublishedEpoch().value_or(0) == 2,
            "publisher should publish second promoted epoch");
        Require(
            cfg::ActionManifestPublisher::GetSingleton().GetActiveEpochObservedAtLastPublish().value_or(0) == 1,
            "publish seam should observe the previous active epoch before active swap");
        const auto epoch = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epoch.has_value() && *epoch == 2, "second promote should swap active after publish");
    }

    // Publisher fail-closed on epoch mismatch: no publication and no publish count increment.
    {
        const auto active = cfg::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        Require(active != nullptr, "active bundle should exist before publisher mismatch test");
        auto mismatched = *active;
        mismatched.manifest.manifestEpoch = active->manifestEpoch + 100;

        cfg::ActionManifestPublisher::GetSingleton().ResetForTests();
        const auto published = cfg::ActionManifestPublisher::GetSingleton().PublishPromotedBundle(
            mismatched,
            active->manifestEpoch);
        Require(!published, "publisher should reject epoch mismatch");
        Require(
            cfg::ActionManifestPublisher::GetSingleton().GetPublishCount() == 0,
            "epoch mismatch must not increment publish count");
        Require(
            !cfg::ActionManifestPublisher::GetSingleton().GetLastPublishedEpoch().has_value(),
            "epoch mismatch must not update last published epoch");
    }

    // Runtime reload failure keeps existing active bundle.
    {
        WriteFile(bindings, "[Gameplay]\nButton:Cross=Game.NotARealAction\n");
        const auto res = cfg::AtomicConfigReloader::GetSingleton().Reload();
        Require(!res.ok, "reload with invalid config should fail");
        const auto epoch = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epoch.has_value() && *epoch == 2, "failed reload must keep active epoch");
    }

    // Startup recovery uses disk last-known-good when config is invalid and no active bundle exists.
    {
        cfg::AtomicConfigReloader::GetSingleton().ResetForTests();
        const auto res = cfg::AtomicConfigReloader::GetSingleton().LoadOrRecover(bindings, policy);
        Require(res.ok, "startup with invalid config should recover from disk LKG");
        Require(res.recoveredFromDiskLkg, "startup recovery should report recoveredFromDiskLkg=true");
        const auto epoch = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epoch.has_value() && *epoch == 2, "recovered epoch should match latest LKG epoch");
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

    // BindingConfig::Reload must report reloader failure while retaining old active bundle.
    {
        const auto temp4 = std::filesystem::temp_directory_path() / "dualpad-inputv2-binding-facade-reload";
        std::filesystem::remove_all(temp4);
        std::filesystem::create_directories(temp4);
        const auto bindings4 = temp4 / "DualPadBindings.ini";
        const auto policy4 = temp4 / "DualPadMenuPolicy.ini";
        WriteFile(bindings4, ReadFile(fixtureBindings));
        WriteFile(policy4, ReadFile(fixturePolicy));

        cfg::AtomicConfigReloader::GetSingleton().ResetForTests();
        cfg::ActionManifestPublisher::GetSingleton().ResetForTests();
        Require(dualpad::input::BindingConfig::GetSingleton().Load(bindings4), "BindingConfig load should succeed");
        const auto epochBefore = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epochBefore.has_value(), "BindingConfig load should promote active bundle");

        WriteFile(bindings4, "[Gameplay]\nButton:Cross=Game.NotARealAction\n");
        Require(!dualpad::input::BindingConfig::GetSingleton().Reload(), "BindingConfig reload should report failure");
        const auto epochAfter = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epochAfter == epochBefore, "BindingConfig failed reload should keep old active bundle");
    }

    // MenuContextPolicy::Reload must report reloader failure while retaining old active bundle.
    {
        const auto temp5 = std::filesystem::temp_directory_path() / "dualpad-inputv2-menu-facade-reload";
        std::filesystem::remove_all(temp5);
        std::filesystem::create_directories(temp5);
        const auto bindings5 = temp5 / "DualPadBindings.ini";
        const auto policy5 = temp5 / "DualPadMenuPolicy.ini";
        WriteFile(bindings5, ReadFile(fixtureBindings));
        WriteFile(policy5, ReadFile(fixturePolicy));

        cfg::AtomicConfigReloader::GetSingleton().ResetForTests();
        cfg::ActionManifestPublisher::GetSingleton().ResetForTests();
        const auto res = cfg::AtomicConfigReloader::GetSingleton().LoadOrRecover(bindings5, policy5);
        Require(res.ok, res.message);
        Require(dualpad::input::MenuContextPolicy::GetSingleton().Load(policy5), "MenuContextPolicy load should sync active bundle");
        const auto epochBefore = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();

        WriteFile(policy5, "[Track]\nBrokenMenu=NotAContext\n");
        Require(!dualpad::input::MenuContextPolicy::GetSingleton().Reload(), "MenuContextPolicy reload should report failure");
        const auto epochAfter = cfg::AtomicConfigReloader::GetSingleton().GetActiveEpoch();
        Require(epochAfter == epochBefore, "MenuContextPolicy failed reload should keep old active bundle");
    }
}
