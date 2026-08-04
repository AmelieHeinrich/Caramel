/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include "ShaderTypes.hpp"

bool CompileStage(const ParsedShaderSource& source, EShaderStage stage, const String& entryPoint,
    uint64 variantMask, TArray<uint8>& outBytecode, uint32 outGroupSize[3]);
