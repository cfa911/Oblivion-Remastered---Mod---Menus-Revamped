-- Menus Revamped: integrated OBSE64 plugin for The Elder Scrolls IV: Oblivion Remastered. GPL-3.0-or-later.
-- /d1trimfile strips the project folder from __FILE__ and std::source_location (no build-machine paths in the DLLs); /PDBALTPATH keeps only the
-- PDB's file name in the debug directory. Both are set BEFORE includes() so CommonLibOB64's own library target gets them too.
add_cxflags({"/d1trimfile:$(projectdir)"}, {force = true, expand = false})
add_shflags("/PDBALTPATH:%_PDB%", {force = true})

includes("lib/commonlibob64")

set_project("MenusRevamped")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")
add_rules("mode.debug", "mode.releasedbg")

-- One development plugin: search, category pickers and controller keyboard.
target("MenusRevamped")
    set_version("1.0.0")
    add_rules("commonlibob64.plugin", {
        name = "MenusRevamped",
        author = "cfa911",
        description = "Unified menu search, category pickers and controller keyboard"
    })
    add_syslinks("user32", "imm32")
    -- Feature folders under src/; every include is a path from src/ ("engine/Ui.h").
    add_files("src/core/*.cpp", "src/engine/*.cpp", "src/search/*.cpp", "src/keyboard/*.cpp", "src/sorting/*.cpp", "src/hover/*.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/core/pch.h")
