set_xmakever("2.8.2")

local clib_path = nil
if os.isdir("lib/commonlibsse-ng") then
    clib_path = "lib/commonlibsse-ng"
elseif os.isdir("../CustomSkillsFrameworkVR/lib/commonlibsse-ng") then
    clib_path = "../CustomSkillsFrameworkVR/lib/commonlibsse-ng"
end

if clib_path then
    includes(clib_path)
end

set_project("AlchemyRecipeViewVR")
set_version("0.1.0")

set_languages("c++23")
set_warnings("allextra")
set_policy("package.requires_lock", true)

add_rules("mode.release")
add_rules("plugin.vsxmake.autoupdate")

target("AlchemyRecipeViewVR")
    if not clib_path then
        raise("commonlibsse-ng not found. Expected lib/commonlibsse-ng or ../CustomSkillsFrameworkVR/lib/commonlibsse-ng")
    end

    add_deps("commonlibsse-ng")

    add_rules("commonlibsse-ng.plugin", {
        name = "AlchemyRecipeViewVR",
        author = "ZerothBear",
        description = "Skyrim VR native rewrite for Alchemy Recipe View",
        options = { NO_PLUGIN_CPP = true }
    })

    add_files("src/**.cpp")
    remove_files("src/Hooks/CraftingMenuHook.cpp")
    remove_files("src/Hooks/FrameProbe.cpp")
    remove_files("src/Events/CraftingMenuWatcher.cpp")
    add_headerfiles("src/**.h", "include/**.h")
    add_includedirs("src", "include")
    set_pcxxheader("src/PCH/PCH.h")
