/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:10:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ShaderCompiler.hpp"

#include <Caramel/Core/Logger.hpp>

#include <cstdlib>
#include <cstring>
#include <algorithm>

bool CompileStage(const ParsedShaderSource& source, EShaderStage stage, const String& entryPoint,
    uint64 variantMask, TArray<uint8>& outBytecode, uint32 outGroupSize[3])
{
    TArray<const char*> defineNames;
    for (uint64 i = 0; i < source.Variants.Size(); ++i) {
        if (variantMask & (1ull << i))
            defineNames.PushBack(source.Variants[i].CStr());
    }

    agfxShaderCompilerOptions options{};
    options.stage = ToAgfxShaderStage(stage);

    size_t entryLen = std::min<size_t>(entryPoint.Size(), sizeof(options.entryPoint) - 1);
    memcpy(options.entryPoint, entryPoint.CStr(), entryLen);
    options.entryPoint[entryLen] = '\0';

    options.definesCount = (uint32)std::min<size_t>(defineNames.Size(), 256);
    for (uint32 i = 0; i < options.definesCount; ++i)
        options.defines[i] = const_cast<char*>(defineNames[i]);

    // agfxShaderCompilerOptions::sourceCode is `char*` (DXC's DxcBuffer only reads it); String only
    // exposes a const pointer, so this cast is safe but required.
    options.sourceCode = const_cast<char*>(source.InlinedSource.CStr());
    options.sourceCodeSize = (uint32)source.InlinedSource.Size();

#if defined(NDEBUG)
    options.addDebugSymbols = 0;
#else
    options.addDebugSymbols = 1;
#endif

    agfxShaderCompilerResult result{};
    agfxCompileShader(&options, &result);

    if (!result.compiledCode || result.compiledSize == 0) {
        CARAMEL_ERROR("ShaderCompiler: failed to compile '{}' entry '{}'", source.ShaderPath.CStr(), entryPoint.CStr());
        return false;
    }

    outBytecode.Resize(result.compiledSize);
    memcpy(outBytecode.Data(), result.compiledCode, result.compiledSize);
    free(result.compiledCode);

    switch (stage) {
        case EShaderStage::Compute:
            outGroupSize[0] = result.tgSizeX; outGroupSize[1] = result.tgSizeY; outGroupSize[2] = result.tgSizeZ;
            break;
        case EShaderStage::Mesh:
            outGroupSize[0] = result.meshSizeX; outGroupSize[1] = result.meshSizeY; outGroupSize[2] = result.meshSizeZ;
            break;
        case EShaderStage::Task:
            outGroupSize[0] = result.taskSizeX; outGroupSize[1] = result.taskSizeY; outGroupSize[2] = result.taskSizeZ;
            break;
        default:
            break;
    }

    return true;
}
