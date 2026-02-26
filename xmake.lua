-- set minimum xmake version
set_xmakever("2.8.2")

-- includes
includes("lib/commonlibsse-ng")

-- set project
set_project("DualPad")
set_version("1.0.0")
set_license("GPL-3.0")

-- set defaults
set_languages("c++23")
set_warnings("allextra")

-- set policies
set_policy("package.requires_lock", true)

-- add rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- packages
add_requires("hidapi")

-- MO2 deploy path
local mo2_plugins_dir = "G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins"

-- target
target("DualPad")
    -- commonlibsse-ng plugin target is a shared library (dll)
    add_deps("commonlibsse-ng")
    add_packages("hidapi")

    add_rules("commonlibsse-ng.plugin", {
        name = "DualPad",
        author = "xuanyuantec",
        description = "DualPad SKSE plugin"
    })

    -- source
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- output directly to MO2 mod folder
    set_targetdir(mo2_plugins_dir)

    -- keep symbols in debug / releasedbg
    if is_mode("debug", "releasedbg") then
        set_symbols("debug")
    end

    -- ensure pdb is copied next to dll (有些情况下不会自动在同目录)
    after_build(function (target)
        local pdb = target:symbolfile()
        if pdb and os.isfile(pdb) then
            os.mkdir(mo2_plugins_dir)
            os.cp(pdb, path.join(mo2_plugins_dir, path.filename(pdb)))
        end
        print("Deployed: %s", target:targetfile())
    end)