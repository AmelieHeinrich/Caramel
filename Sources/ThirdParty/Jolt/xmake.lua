-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-03 20:33:27
-- @ Copyright: Day III Digital - All rights reserved
-- 

target("Jolt")
    set_kind("static")
    add_files("**.cpp")
    add_defines("JPH_DEBUG_RENDERER", { public = true })
