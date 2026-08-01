-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-01 21:06:15
-- @ Copyright: Day III Digital - All rights reserved
-- 

target("agfx_shader")
    set_kind("static")
    
    if is_plat("macosx") then
        add_files("agfx_shader_compiler_mac.mm")
        add_links("metalirconverter", "dxcompiler")
    elseif is_plat("windows") then
        add_files("agfx_shader_compiler_windows.cpp")
        add_syslinks("dxcompiler", { public = true })
    elseif is_plat("linux") then
        add_files("agfx_shader_compiler_linux.cpp")
        add_files("spirv_reflect/spirv_reflect.c")
        add_syslinks("dl", { public = true })
    end