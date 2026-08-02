/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <AGFX/agfx.hpp>
#include <AGFXShader/agfx_shader_compiler.h>

enum class EShaderStage : uint8
{
    Vertex,
    Fragment,
    Compute,
    Task,
    Mesh
};

inline agfxShaderStage ToAgfxShaderStage(EShaderStage stage)
{
    switch (stage) {
        case EShaderStage::Vertex:   return AGFX_SHADER_STAGE_VERTEX;
        case EShaderStage::Fragment: return AGFX_SHADER_STAGE_FRAGMENT;
        case EShaderStage::Compute:  return AGFX_SHADER_STAGE_COMPUTE;
        case EShaderStage::Task:     return AGFX_SHADER_STAGE_TASK;
        case EShaderStage::Mesh:     return AGFX_SHADER_STAGE_MESH;
    }
    return AGFX_SHADER_STAGE_VERTEX;
}

inline agfx::ShaderModuleType ToAgfxShaderModuleType(EShaderStage stage)
{
    switch (stage) {
        case EShaderStage::Vertex:   return agfx::ShaderModuleType::Vertex;
        case EShaderStage::Fragment: return agfx::ShaderModuleType::Fragment;
        case EShaderStage::Compute:  return agfx::ShaderModuleType::Compute;
        case EShaderStage::Task:     return agfx::ShaderModuleType::Task;
        case EShaderStage::Mesh:     return agfx::ShaderModuleType::Mesh;
    }
    return agfx::ShaderModuleType::Vertex;
}

// Maximum number of `#pragma variant` toggles a single shader file may declare (bit-width of the variant mask).
static constexpr uint64 kMaxShaderVariants = 64;

// The result of parsing a shader file: its fully include-inlined source text, every file that
// contributed to it (for hot-reload dependency tracking), and the pragma-declared stages/variants.
struct ParsedShaderSource
{
    String ShaderPath;
    String InlinedSource;
    TArray<String> DependencyFiles;
    TDictionary<EShaderStage, String> StageEntryPoints;
    TArray<String> Variants;
};

// Raw compiled bytecode produced off the render thread (by the watcher thread, or synchronously
// on first use). Never holds an AGFX handle -- only the render thread is allowed to realize these
// into live agfx::ShaderModule/Pipeline objects.
struct PendingCompileResult
{
    String ShaderPath;
    uint64 VariantMask = 0;
    TDictionary<EShaderStage, TArray<uint8>> Bytecode;
    uint32 GroupSize[3] = { 1, 1, 1 };
    ParsedShaderSource NewSource;
};

enum class EPipelineKind
{
    Render,
    Compute
};

// A single compiled variant's live GPU objects. Move-only (agfx::Handle-derived members delete
// their copy constructor), so it can never be accidentally copied across threads.
struct CompiledVariant
{
    TDictionary<EShaderStage, agfx::ShaderModule> Modules;
    agfx::RenderPipeline RenderPipeline;
    agfx::ComputePipeline ComputePipeline;
};

// A shader registered via ShaderServer::RegisterRenderPipeline/RegisterComputePipeline: the
// caller's pipeline template (shader-module fields left blank), the current parse of its source,
// and a cache of one CompiledVariant per requested variant bitmask.
struct RegisteredPipeline
{
    EPipelineKind Kind = EPipelineKind::Render;
    agfx::RenderPipelineCreateInfo RenderTemplate;
    agfx::ComputePipelineCreateInfo ComputeTemplate;
    ParsedShaderSource Source;
    TDictionary<uint64, CompiledVariant> VariantCache;
};
