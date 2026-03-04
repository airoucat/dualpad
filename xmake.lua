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
add_requires("minhook")

-- build_legacy 开关：默认 false，不编译 src/legacy 下 cpp
option("build_legacy")
    set_default(false)
    set_showmenu(true)
    set_description("Build legacy modules under src/legacy")
option_end()

local mo2_plugins_dir = "G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins"

target("DualPad")
    set_kind("shared")
    add_deps("commonlibsse-ng")
    add_packages("hidapi", "minhook")
    add_options("build_legacy")

    -- 不走 XAPO，移除 xapobase
    add_syslinks("gdi32", "gdiplus", "user32", "xaudio2")

    add_rules("commonlibsse-ng.plugin", {
        name = "DualPad",
        author = "xuanyuantec",
        description = "DualSense support for Skyrim"
    })

    -- 文件收集：默认排除 legacy
    if has_config("build_legacy") then
        add_files("src/**.cpp")
        add_includedirs("src", "src/legacy")
        add_defines("DP_BUILD_LEGACY=1")
    else
        add_files("src/**.cpp|src/legacy/**.cpp")
        -- 双保险（即使上面误配也排掉）
        remove_files("src/legacy/**.cpp")
        add_includedirs("src")
        add_defines("DP_BUILD_LEGACY=0")
    end

    add_headerfiles("src/**.h")
    set_pcxxheader("src/pch.h")
    set_targetdir(mo2_plugins_dir)

    if is_mode("debug", "releasedbg") then
        set_symbols("debug")
    end

    -- releasedbg 做安全裁剪（不激进）
    if is_mode("releasedbg") then
        add_cxflags("/Gy", {force = true})
        add_ldflags("/OPT:REF", "/OPT:ICF", {force = true})
    end

    after_build(function (target)
        print("Deployed: %s", target:targetfile())
    end)