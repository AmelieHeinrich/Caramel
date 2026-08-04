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
        String outputDirectory;
        bool verbose = false;
    };

    class Compressor
    {
    public:
        bool Compile(const String& inputGltfPath, const CompilerOptions& options);
    };
}
