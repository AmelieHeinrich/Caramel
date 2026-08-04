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

static constexpr uint64 kMaxShaderVariants = 64;

struct ParsedShaderSource
{
    String ShaderPath;
    String InlinedSource;
    TArray<String> DependencyFiles;
    TDictionary<EShaderStage, String> StageEntryPoints;
    TArray<String> Variants;
};

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

struct CompiledVariant
{
    TDictionary<EShaderStage, agfx::ShaderModule> Modules;
    agfx::RenderPipeline RenderPipeline;
    agfx::ComputePipeline ComputePipeline;
};

struct RegisteredPipeline
{
    EPipelineKind Kind = EPipelineKind::Render;
    agfx::RenderPipelineCreateInfo RenderTemplate;
    agfx::ComputePipelineCreateInfo ComputeTemplate;
    ParsedShaderSource Source;
    TDictionary<uint64, CompiledVariant> VariantCache;
};
