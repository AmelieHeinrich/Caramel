-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-01 21:05:24
-- @ Copyright: Day III Digital - All rights reserved
-- 

target("agfx")
    set_kind("static")
    set_warnings("all", "error")

    if is_plat("macosx") then
        add_files("agfx_metal4.mm")
        add_frameworks("Metal", "QuartzCore", "CoreGraphics", { public = true })
    elseif is_plat("windows") then
        add_files("agfx_d3d12.cpp")
        add_syslinks("d3d12", "dxgi", "dxguid", "WinPixEventRuntime", { public = true })
        add_defines("USE_PIX")
    elseif is_plat("linux") then
        add_files("agfx_vulkan.cpp")
        add_includedirs("vk")
    end
