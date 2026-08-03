/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 19:15:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <CaramelAsset/Compressor.hpp>

#include <spdlog/spdlog.h>

#include <cstring>

namespace
{
    void PrintUsage()
    {
        spdlog::info("Usage: CaramelAssetCompiler <input.gltf> [-o <output_dir>] [--verbose]");
        spdlog::info("  -o defaults to Content/Cache/<AssetName>/, tracked in Content/Cache/cache.json");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    String inputPath;
    CaramelAsset::CompilerOptions options;

    for (int i = 1; i < argc; i++)
    {
        const char* arg = argv[i];
        if ((std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) && i + 1 < argc)
        {
            options.outputDirectory = argv[++i];
        }
        else if (std::strcmp(arg, "--verbose") == 0)
        {
            options.verbose = true;
        }
        else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0)
        {
            PrintUsage();
            return 0;
        }
        else if (inputPath.Empty())
        {
            inputPath = arg;
        }
        else
        {
            spdlog::error("Unknown argument: {}", arg);
            PrintUsage();
            return 1;
        }
    }

    if (inputPath.Empty())
    {
        PrintUsage();
        return 1;
    }

    CaramelAsset::Compressor compressor;
    bool success = compressor.Compile(inputPath, options);

    return success ? 0 : 1;
}
