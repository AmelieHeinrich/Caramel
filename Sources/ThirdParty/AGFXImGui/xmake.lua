-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-01 21:05:54
-- @ Copyright: Day III Digital - All rights reserved
-- 

target("agfx_imgui")
    set_kind("static")
    add_files("*.cpp")

    add_deps("agfx")
    add_packages("imgui", { public = true })
