/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include "ShaderTypes.hpp"

// Compiles a single stage of `source` (already fully include-inlined) to bytecode via
// agfxCompileShader, with the variants set in `variantMask` (bit i = source.Variants[i] enabled)
// passed as compiler defines. Pure CPU-side (spins up its own DXC instances per call, touches no
// agfxDevice) -- safe to call from any thread.
//
// Zero-initializes the compiler's options/result structs, since agfxCompileShader has no
// structured failure signal: every one of its failure paths leaves `compiledCode` untouched and
// only logs to stderr. Returns false (outBytecode/outGroupSize untouched) on failure; the caller
// is responsible for logging shader/stage/entry-point context via CARAMEL_ERROR, since
// agfxCompileShader's own diagnostics don't reach the engine's logger.
bool CompileStage(const ParsedShaderSource& source, EShaderStage stage, const String& entryPoint,
    uint64 variantMask, TArray<uint8>& outBytecode, uint32 outGroupSize[3]);
