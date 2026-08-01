-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-01 20:43:51
-- @ Copyright: Day III Digital - All rights reserved
-- 

add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_requires("libsdl3", "spdlog", "glm")
add_requires("imgui", { configs = { docking = true, sdl3 = true } })

-- General defines
set_rundir(".")
set_languages("cxx20")
add_includedirs("Sources", "Sources/ThirdParty", { public = true })

if is_plat("windows") then
    add_defines("CARAMEL_WINDOWS", "USE_PIX", { public = true })
    add_linkdirs("Content/Binaries/Win64", { public = true })

    before_build(function (target)
        os.cp("Content/Binaries/Win64/*", target:targetdir())
    end)
elseif is_plat("linux") then
    add_defines("CARAMEL_LINUX", { public = true })
    add_linkdirs("Content/Binaries/Linux", { public = true })
elseif is_plat("macosx") then
    add_defines("CARAMEL_MACOS", { public = true })
    add_linkdirs("Content/Binaries/Mac", { public = true })
    add_cxxflags("-fobjc-arc", "-x objective-c++", { public = true })
end

includes("Sources")
