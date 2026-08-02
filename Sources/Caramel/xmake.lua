-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-01 20:44:22
-- @ Copyright: Day III Digital - All rights reserved
-- 

target("Caramel")
    set_kind("binary")
    add_files("**.cpp")

    add_deps("agfx", "agfx_shader")
    add_packages("libsdl3", "glm", "spdlog", "imgui")
