-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-03 08:23:24
-- @ Copyright: Day III Digital - All rights reserved
-- 

target("CaramelAssetCompiler")
    set_kind("binary")
    add_files("main.cpp")

    add_deps("CaramelAsset", "Jolt")
    add_packages("spdlog")
