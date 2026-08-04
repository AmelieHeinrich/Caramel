--
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-04 00:00:00
-- @ Copyright: Day III Digital - All rights reserved
--

target("AngelScript")
    set_kind("static")
    set_languages("cxx20")

    -- Library core (arch specific .cpp files are guarded by as_config.h)
    add_files("*.cpp")

    -- Add-ons
    add_files("add_on/contextmgr/*.cpp")
    add_files("add_on/datetime/*.cpp")
    add_files("add_on/debugger/*.cpp")
    add_files("add_on/scriptany/*.cpp")
    add_files("add_on/scriptarray/*.cpp")
    add_files("add_on/scriptbuilder/*.cpp")
    add_files("add_on/scriptdictionary/*.cpp")
    add_files("add_on/scriptfile/*.cpp")
    add_files("add_on/scriptgrid/*.cpp")
    add_files("add_on/scripthandle/*.cpp")
    add_files("add_on/scripthelper/*.cpp")
    add_files("add_on/scriptmath/*.cpp")
    add_files("add_on/scriptsocket/*.cpp")
    add_files("add_on/scriptstdstring/*.cpp")
    add_files("add_on/serializer/*.cpp")
    add_files("add_on/weakref/*.cpp")
    -- autowrapper is header only

    -- Consumers include as <angelscript.h> / <add_on/...>
    add_includedirs(".", { public = true })

    if is_plat("windows") then
        add_defines("WIN32", "_CRT_SECURE_NO_WARNINGS")
        add_syslinks("ws2_32", { public = true }) -- scriptsocket
        if is_arch("arm64") then
            add_files("as_callfunc_arm64_msvc.asm")
        elseif is_arch("x64", "x86_64") then
            add_files("as_callfunc_x64_msvc_asm.asm")
        elseif is_arch("arm.*") then
            add_files("as_callfunc_arm_msvc.asm")
        end
    elseif is_plat("macosx") then
        -- The root project forces objective-c++ on every TU, undo that here
        add_cxxflags("-x c++", { force = true })
        if is_arch("arm64") then
            add_files("as_callfunc_arm64_xcode.S")
        end
    elseif is_plat("linux") then
        add_syslinks("pthread", { public = true })
        if is_arch("arm64", "aarch64") then
            add_files("as_callfunc_arm64_gcc.S")
        elseif is_arch("arm.*") then
            add_files("as_callfunc_arm_gcc.S")
        end
    end
