/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 19:15:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <CaramelAsset/Compressor.hpp>

#include <spdlog/spdlog.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <cstring>

namespace
{
    void PrintUsage()
    {
        spdlog::info("Usage: CaramelAssetCompiler <input.gltf> [-o <output_dir>] [--verbose] [--skip-textures]");
        spdlog::info("  -o defaults to Content/Cache/<AssetName>/, tracked in Content/Cache/cache.json");
        spdlog::info("  --skip-textures skips texture compression, reusing .ctex files from a prior full bake");
        spdlog::info("    in the same cache folder -- fast mesh/collider-only rebake, not a first-bake mode");
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
        else if (std::strcmp(arg, "--skip-textures") == 0)
        {
            options.skipTextures = true;
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

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    CaramelAsset::Compressor compressor;
    bool success = compressor.Compile(inputPath, options);

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    return success ? 0 : 1;
}
