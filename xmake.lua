set_xmakever("2.8.2")

includes("lib/commonlibsse-ng")

set_project("DualPad")
set_version("1.0.0")
set_license("GPL-3.0")

set_languages("c++23")
set_warnings("allextra")
set_policy("check.auto_ignore_flags", false)
set_policy("package.requires_lock", true)

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_requires("hidapi")

option("dualpad_deploy")
    set_default(false)
    set_showmenu(true)
    set_description("Copy built artifacts to a local Skyrim/MO2 install after build.")
option_end()

option("dualpad_mo2_plugins_dir")
    set_default(os.getenv("DUALPAD_MO2_PLUGINS_DIR") or "")
    set_showmenu(true)
    set_description("SKSE Plugins directory used when --dualpad_deploy=true.")
option_end()

option("dualpad_skyrim_game_dir")
    set_default(os.getenv("DUALPAD_SKYRIM_GAME_DIR") or "")
    set_showmenu(true)
    set_description("Skyrim game directory used when --dualpad_deploy=true.")
option_end()

local dualpad_deploy = get_config("dualpad_deploy")
local mo2_plugins_dir = get_config("dualpad_mo2_plugins_dir") or ""
local skyrim_game_dir = get_config("dualpad_skyrim_game_dir") or ""

local function has_path(value)
    return value and value ~= ""
end

local function artifact_dir(name)
    return path.join(os.projectdir(), "build", "bin", name)
end

local function configured_target_dir(deploy_dir, fallback_name)
    if dualpad_deploy and has_path(deploy_dir) then
        return deploy_dir
    end
    return artifact_dir(fallback_name)
end

target("DualPad")
    add_deps("commonlibsse-ng")
    add_packages("hidapi")

    add_syslinks("gdi32", "gdiplus", "ole32", "shell32", "user32", "windowscodecs")

    add_rules("commonlibsse-ng.plugin", {
        name = "DualPad",
        author = "xuanyuantec",
        description = "DualSense support for Skyrim"
    })

    add_files("src/**.cpp")
    remove_files("src/input_v2/telemetry/ReplayHarnessMain.cpp")
    remove_files("src/input_v2/telemetry/*ReplayStub.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    set_targetdir(configured_target_dir(mo2_plugins_dir, "DualPad"))

    if is_mode("debug", "releasedbg") then
        set_symbols("debug")
        add_defines("DUALPAD_DIAGNOSTIC_BUILD")
    end

    after_build(function (target)
        if not dualpad_deploy then
            print("Built: %s", target:targetfile())
            return
        end

        if not has_path(mo2_plugins_dir) then
            print("warning: skip DualPad deploy: dualpad_mo2_plugins_dir or DUALPAD_MO2_PLUGINS_DIR is not set")
            print("Built: %s", target:targetfile())
            return
        end

        local pdb = target:symbolfile()
        if pdb and os.isfile(pdb) then
            os.mkdir(mo2_plugins_dir)
            try {
                function ()
                    os.cp(pdb, path.join(mo2_plugins_dir, path.filename(pdb)))
                end,
                catch {
                    function (err)
                        print("warning: skip pdb copy: %s", err)
                    end
                }
            }
        end
        local debug_ini_src = path.join(os.projectdir(), "config", "DualPadDebug.ini")
        local debug_ini_dst = path.join(mo2_plugins_dir, "DualPadDebug.ini")
        local menu_policy_ini_src = path.join(os.projectdir(), "config", "DualPadMenuPolicy.ini")
        local menu_policy_ini_dst = path.join(mo2_plugins_dir, "DualPadMenuPolicy.ini")
        local controlmap_overlay_src = path.join(
            os.projectdir(),
            "config",
            "controlmap_profiles",
            "DualPadNativeCombo",
            "Interface",
            "Controls",
            "PC",
            "controlmap.txt")
        local controlmap_overlay_dst = path.join(mo2_plugins_dir, "DualPadControlMap.txt")
        if os.isfile(debug_ini_src) and not os.isfile(debug_ini_dst) then
            os.mkdir(mo2_plugins_dir)
            os.cp(debug_ini_src, debug_ini_dst)
        end
        if os.isfile(menu_policy_ini_src) and not os.isfile(menu_policy_ini_dst) then
            os.mkdir(mo2_plugins_dir)
            os.cp(menu_policy_ini_src, menu_policy_ini_dst)
        end
        if os.isfile(controlmap_overlay_src) then
            os.mkdir(mo2_plugins_dir)
            os.cp(controlmap_overlay_src, controlmap_overlay_dst)
        end
        print("Deployed: %s", target:targetfile())
    end)

local ph1_manifest_compiler_files = {
    "src/input_v2/config/LegacyIniImporter.cpp",
    "src/input_v2/context/ContextCatalog.cpp",
    "src/input_v2/actions/ActionManifest.cpp",
    "src/input_v2/config/ManifestValidator.cpp",
    "src/input_v2/config/AtomicConfigReloader.cpp",
    "src/input_v2/config/ActionManifestPublisher.cpp"
}

local ph2_context_resolver_files = {
    "src/input_v2/menu/UiMenuObserver.cpp",
    "src/input_v2/menu/MenuInstanceRegistry.cpp",
    "src/input_v2/context/ContextResolver.cpp",
    "src/input_v2/context/ContextRefreshTick.cpp",
    "src/input_v2/actions/ActionSetResolver.cpp"
}

local ph4_action_graph_files = {
    "src/input_v2/actions/ControlPath.cpp",
    "src/input_v2/actions/InteractionSpec.cpp",
    "src/input_v2/actions/CompiledActionGraph.cpp",
    "src/input_v2/actions/CompiledActionGraphPublisher.cpp",
    "src/input_v2/actions/InteractionEngine.cpp",
    "src/input_v2/actions/LegacyInteractionInputAdapter.cpp",
    "src/input_v2/actions/LegacyLifecycleBridge.cpp"
}

target("DualPadMenuContextPolicyTests")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")
    add_defines("DUALPAD_ENABLE_TEST_ONLY_MENU_POLICY_PARSE")

    add_files(
        "tests/MenuContextPolicyTests.cpp",
        "src/input/InputContextNames.cpp",
        "src/input/MenuContextPolicy.cpp")
    add_files(table.unpack(ph1_manifest_compiler_files))
    add_files(table.unpack(ph4_action_graph_files))
    add_headerfiles("tests/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("DualPadManifestCompilerTests")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")

    add_files("tests/input_v2/**.cpp")
    remove_files("tests/input_v2/ContextResolverTests.cpp")
    remove_files("tests/input_v2/PresentationProjectionTests.cpp")
    add_files(table.unpack(ph1_manifest_compiler_files))
    add_files(table.unpack(ph4_action_graph_files))
    add_files(
        "src/input/BindingManager.cpp",
        "src/input/BindingConfig.cpp",
        "src/input/InputContextNames.cpp",
        "src/input/MenuContextPolicy.cpp")
    add_headerfiles("tests/**.h")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("DualPadContextResolverTests")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")

    add_files("tests/input_v2/ContextResolverTests.cpp")
    add_files(table.unpack(ph1_manifest_compiler_files))
    add_files(table.unpack(ph4_action_graph_files))
    add_files(table.unpack(ph2_context_resolver_files))
    add_files("src/input/InputContext.cpp")
    add_headerfiles("tests/**.h")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("DualPadPresentationProjectionTests")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")

    add_files("tests/input_v2/PresentationProjectionTests.cpp")
    add_files(table.unpack(ph1_manifest_compiler_files))
    add_files(table.unpack(ph4_action_graph_files))
    add_files(table.unpack(ph2_context_resolver_files))
    add_files(
        "src/input_v2/presentation/SourceEvidenceCollector.cpp",
        "src/input_v2/presentation/GameplayPresentationAdapter.cpp",
        "src/input_v2/presentation/PresentationProjection.cpp",
        "src/input_v2/presentation/SkyrimCompatibilitySurface.cpp",
        "src/input/InputContext.cpp")
    add_headerfiles("tests/**.h")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("DualPadInputV2Tests")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")

    add_files("tests/input_v2/InputV2Tests.cpp")
    add_files(table.unpack(ph4_action_graph_files))
    add_files(table.unpack(ph1_manifest_compiler_files))
    add_files(
        "src/input/BindingManager.cpp",
        "src/input/BindingConfig.cpp",
        "src/input/InputContextNames.cpp",
        "src/input/MenuContextPolicy.cpp")
    add_headerfiles("tests/**.h")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("DualPadRouteHealthContractTests")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")

    add_files(
        "tests/RouteHealthContractTests.cpp",
        "src/input/injection/RouteHealthContract.cpp")
    add_headerfiles("tests/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("DualPadGlyphResolutionCompatTests")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")

    add_files(
        "tests/GlyphResolutionCompatTests.cpp",
        "src/input/BindingManager.cpp",
        "src/input/InputContextNames.cpp",
        "src/input/glyph/GlyphResolutionCompat.cpp")
    add_headerfiles("tests/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

local replay_runtime_files = {
    "src/input_v2/telemetry/TraceSchema.cpp",
    "src/input_v2/telemetry/InputTraceRecorder.cpp",
    "src/input_v2/telemetry/ReplayHarness.cpp",
    "src/input_v2/telemetry/ActionExecutorReplayStub.cpp",
    "src/input_v2/telemetry/ContextManagerReplayStub.cpp",
    "src/input_v2/telemetry/GameplayKbmFactTrackerReplayStub.cpp",
    "src/input_v2/telemetry/InputModalityTrackerReplayStub.cpp",
    "src/input_v2/telemetry/NativeButtonCommitBackendReplayStub.cpp",
    "src/input_v2/telemetry/ScaleformGlyphBridgeReplayStub.cpp",
    "src/input_v2/telemetry/UpstreamGamepadHookReplayStub.cpp",
    "src/input/ActionDispatcher.cpp",
    "src/input/AuthoritativePollState.cpp",
    "src/input/BindingConfig.cpp",
    "src/input/BindingManager.cpp",
    "src/input/InputContextNames.cpp",
    "src/input/RuntimeConfig.cpp",
    "src/input/XInputButtonSerialization.cpp",
    "src/input/backend/ActionBackendPolicy.cpp",
    "src/input/backend/ActionLifecycleCoordinator.cpp",
    "src/input/backend/FrameActionPlanDebugLogger.cpp",
    "src/input/backend/FrameActionPlanner.cpp",
    "src/input/backend/KeyboardHelperBackend.cpp",
    "src/input/backend/KeyboardNativeBridge.cpp",
    "src/input/backend/NativeActionDescriptor.cpp",
    "src/input/glyph/GlyphResolutionCompat.cpp",
    "src/input/injection/AxisProjection.cpp",
    "src/input/injection/GameplayOwnershipCoordinator.cpp",
    "src/input/injection/PadEventSnapshotDispatcher.cpp",
    "src/input/injection/PadEventSnapshotProcessor.cpp",
    "src/input/injection/RouteHealthContract.cpp",
    "src/input/injection/SourceBlockCoordinator.cpp",
    "src/input/injection/SyntheticStateDebugLogger.cpp",
    "src/input/injection/SyntheticStateReducer.cpp",
    "src/input/injection/UnmanagedDigitalPublisher.cpp",
    "src/input/mapping/BindingResolver.cpp",
    "src/input/mapping/TriggerMapper.cpp"
}

for _, file in ipairs(ph1_manifest_compiler_files) do
    table.insert(replay_runtime_files, file)
end

for _, file in ipairs(ph4_action_graph_files) do
    table.insert(replay_runtime_files, file)
end

for _, file in ipairs(ph2_context_resolver_files) do
    table.insert(replay_runtime_files, file)
end

target("DualPadReplayHarness")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")
    add_defines("DUALPAD_REPLAY_HARNESS")

    add_files(table.unpack(replay_runtime_files))
    add_files("src/input_v2/telemetry/ReplayHarnessMain.cpp")
    add_headerfiles("src/input_v2/telemetry/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("DualPadReplayHarnessTests")
    set_kind("binary")
    add_deps("commonlibsse-ng")
    add_syslinks("ole32", "user32")
    add_defines("DUALPAD_REPLAY_HARNESS")

    add_files("tests/ReplayHarnessTests.cpp")
    add_files(table.unpack(replay_runtime_files))
    add_headerfiles("tests/**.h")
    add_headerfiles("src/input_v2/telemetry/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("DualPadDInput8Proxy")
    set_kind("shared")
    set_basename("dinput8")
    set_objectdir(path.join(os.projectdir(), "build", "obj", "DualPadDInput8Proxy"))

    add_files("tools/dinput8_proxy/**.cpp")
    add_files("src/input/backend/KeyboardNativeBridge.cpp")
    add_files("tools/dinput8_proxy/dinput8.def")
    add_headerfiles("tools/dinput8_proxy/**.h")
    add_headerfiles("src/input/backend/KeyboardNativeBridge.h")
    add_includedirs("tools/dinput8_proxy")
    add_includedirs("src")

    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", "DIRECTINPUT_VERSION=0x0800")
    add_syslinks("ole32", "user32", "dxguid")

    set_targetdir(configured_target_dir(skyrim_game_dir, "DualPadDInput8Proxy"))

    if is_mode("debug", "releasedbg") then
        set_symbols("debug")
        add_cxflags("/FS", {tools = "cl"})
    end

    after_build(function (target)
        if not dualpad_deploy then
            print("Built dinput8 proxy: %s", target:targetfile())
            return
        end

        if not has_path(skyrim_game_dir) then
            print("warning: skip dinput8 proxy deploy: dualpad_skyrim_game_dir or DUALPAD_SKYRIM_GAME_DIR is not set")
            print("Built dinput8 proxy: %s", target:targetfile())
            return
        end

        local pdb = target:symbolfile()
        if pdb and os.isfile(pdb) then
            try {
                function ()
                    os.cp(pdb, path.join(skyrim_game_dir, path.filename(pdb)))
                end,
                catch {
                    function (err)
                        print("warning: skip dinput8 proxy pdb copy: %s", err)
                    end
                }
            }
        end
        local proxy_ini_src = path.join(os.projectdir(), "tools", "dinput8_proxy", "DualPadDInput8.ini")
        local proxy_ini_dst = path.join(skyrim_game_dir, "DualPadDInput8.ini")
        if os.isfile(proxy_ini_src) and not os.isfile(proxy_ini_dst) then
            os.cp(proxy_ini_src, proxy_ini_dst)
        end
        print("Deployed dinput8 proxy: %s", target:targetfile())
    end)
