#include "pch.h"

#include "input/BindingConfig.h"

#include "input/BindingManager.h"
#include "input/Trigger.h"
#include "input_v2/config/AtomicConfigReloader.h"

#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        std::filesystem::path DeriveSiblingMenuPolicyPath(const std::filesystem::path& bindingsPath)
        {
            if (bindingsPath.empty()) {
                return {};
            }
            const auto sibling = bindingsPath.parent_path() / "DualPadMenuPolicy.ini";
            if (std::filesystem::exists(sibling)) {
                return sibling;
            }
            return {};
        }

        void MaterializeBindingProjection(const dualpad::input_v2::actions::LegacyBindingProjection& projection)
        {
            auto& manager = BindingManager::GetSingleton();
            manager.ClearBindings();

            for (const auto& entry : projection.bindings) {
                Binding binding{};
                binding.context = entry.context;
                binding.trigger = entry.trigger;
                binding.actionId = entry.actionId;
                manager.AddBinding(binding);
            }
        }
    }

    BindingConfig& BindingConfig::GetSingleton()
    {
        static BindingConfig instance;
        return instance;
    }

    std::filesystem::path BindingConfig::GetDefaultPath()
    {
        return "Data/SKSE/Plugins/DualPadBindings.ini";
    }

    TouchpadConfig BindingConfig::GetTouchpadConfig() const
    {
        return _touchpadConfig;
    }

    bool BindingConfig::Load(const std::filesystem::path& path)
    {
        _configPath = path.empty() ? GetDefaultPath() : path;

        // Phase 1: BindingConfig is a compatibility facade; the runtime truth is the compiled manifest.
        // The path is still accepted as a hint/override for legacy locations and replay harness runs.
        logger::info("[DualPad][Config] Loading bindings via PH1 compiler (path hint: {})", _configPath.string());

        const auto menuPolicyPath = DeriveSiblingMenuPolicyPath(_configPath);
        auto compileResult = dualpad::input_v2::config::LoadOrRecoverResult{ .ok = true, .message = "skipped" };

        // Avoid double-promote on startup when main() already called AtomicConfigReloader::LoadOrRecover().
        const auto activeBefore = dualpad::input_v2::config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        const bool needsCompile =
            !activeBefore ||
            activeBefore->imported.bindingsPath != _configPath ||
            (!menuPolicyPath.empty() && activeBefore->imported.menuPolicyPath != menuPolicyPath);

        if (needsCompile) {
            compileResult =
                dualpad::input_v2::config::AtomicConfigReloader::GetSingleton().LoadOrRecover(_configPath, menuPolicyPath);
            if (!compileResult.ok) {
                logger::warn("[DualPad][Config] AtomicConfigReloader compile failed: {}", compileResult.message);
            }
        }

        const auto bundle = dualpad::input_v2::config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        if (!bundle) {
            logger::error("[DualPad][Config] No active compiled bundle; bindings materialization failed");
            return false;
        }

        MaterializeBindingProjection(bundle->manifest.legacyBindingProjection);
        _touchpadConfig = bundle->manifest.legacyBindingProjection.touchpadConfig;

        logger::info(
            "[DualPad][Config] Materialized {} bindings (epoch={})",
            bundle->manifest.legacyBindingProjection.bindings.size(),
            bundle->manifest.legacyBindingProjection.manifestEpoch);
        return compileResult.ok;
    }

    bool BindingConfig::Reload()
    {
        // Phase 1: runtime reload is owned by AtomicConfigReloader; this facade just re-materializes.
        const auto reloadResult = dualpad::input_v2::config::AtomicConfigReloader::GetSingleton().Reload();
        if (!reloadResult.ok) {
            logger::warn("[DualPad][Config] AtomicConfigReloader reload failed: {}", reloadResult.message);
            return false;
        }
        // Load() will skip a second compile and only materialize the active bundle.
        return Load(_configPath);
    }
}
