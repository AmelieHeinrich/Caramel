/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:54:07
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

namespace CaramelAsset
{
    struct CompilerOptions
    {
        // If empty, output goes to Content/Cache/<AssetName>/ and the compile is recorded in
        // Content/Cache/cache.json (source path -> cache folder), keeping Content/Cache/ a single,
        // disposable, easy-to-gitignore directory. Passing an explicit path opts out of that bookkeeping.
        String outputDirectory;
        bool verbose = false;
    };

    // Compiles a source glTF (.gltf/.glb) into one .cmdl (meshlets/LODs/tangents/skeleton/animations),
    // one .ctex per referenced source texture (BC7/BC5/BC4 on Windows/Linux, ASTC4x4 on macOS) under
    // Textures/, and one standalone, hand-editable .json per material under Materials/ -- all written
    // into CompilerOptions::outputDirectory.
    class Compressor
    {
    public:
        bool Compile(const String& inputGltfPath, const CompilerOptions& options);
    };
}
