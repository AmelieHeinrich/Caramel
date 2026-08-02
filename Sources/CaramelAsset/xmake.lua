-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-02 18:50:09
-- @ Copyright: Day III Digital - All rights reserved
-- 

add_requires("meshoptimizer", "cgltf", "stb")

if is_plat("macosx") then
    add_requires("astc-encoder")
else
    add_requires("compressonator")
end

target("CaramelAsset")
    set_kind("static")
    add_files("*.cpp")
    add_packages("meshoptimizer", "cgltf", "stb")

    if is_plat("macosx") then
        add_files("ASTCCompressor/ASTCCompressor.cpp")
        add_packages("astc-encoder")
    else
        add_files("AMDCompressor/AMDTextureCompressor.cpp")
        add_packages("compressonator")
    end
