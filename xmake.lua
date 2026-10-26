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
    set_version("0.0.0")
    add_rules("commonlibob64.plugin", {
        name = "MenusRevamped",
        author = "cfa911",
        description = "Unified menu search, category pickers and controller keyboard"
    })
    add_syslinks("user32", "imm32")
    add_files("sortingoverhaul/Icons.cpp", "sortingoverhaul/GearFilter.cpp", "sortingoverhaul/GearData.cpp", "sortingoverhaul/Runtime.cpp", "sortingoverhaul/Container.cpp", "sortingoverhaul/Driver.cpp", "sortingoverhaul/main.cpp")
    add_files("revamped/Config.cpp", "menusearch/MenuSearch.cpp", "menusearch/Config.cpp", "menusearch/Quests.cpp",
        "searchkb/Config.cpp", "searchkb/SearchApi.cpp", "searchkb/SearchKeyboard.cpp", "searchkb/XInputHook.cpp")
    add_files("src/Ui.cpp", "src/Reflect.cpp", "src/PEHook.cpp", "src/Menus.cpp", "src/UiText.cpp")

    add_headerfiles("sortingoverhaul/*.h", "menusearch/*.h", "searchkb/*.h", "revamped/*.h")
    add_includedirs("src", "sortingoverhaul")
    set_pcxxheader("src/pch.h")
