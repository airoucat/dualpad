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

local mo2_plugins_dir = "G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins"

target("DualPad")
    add_deps("commonlibsse-ng")
    add_packages("hidapi")

    add_rules("commonlibsse-ng.plugin", {
        name = "DualPad",
        author = "xuanyuantec",
        description = "DualSense support for Skyrim"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    set_targetdir(mo2_plugins_dir)

    if is_mode("debug", "releasedbg") then
        set_symbols("debug")
    end

    after_build(function (target)
        local pdb = target:symbolfile()
        if pdb and os.isfile(pdb) then
            os.mkdir(mo2_plugins_dir)
            os.cp(pdb, path.join(mo2_plugins_dir, path.filename(pdb)))
        end
        print("Deployed: %s", target:targetfile())
    end)