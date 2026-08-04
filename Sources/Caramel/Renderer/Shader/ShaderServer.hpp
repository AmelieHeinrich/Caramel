/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:15:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include "ShaderTypes.hpp"

#include <atomic>
#include <initializer_list>
#include <mutex>
#include <thread>

class Renderer;

class ShaderServer
{
public:
    static void Initialize(agfx::Device& device, Renderer& renderer);

    static void Shutdown();

    static void RegisterRenderPipeline(const agfx::RenderPipelineCreateInfo& pipelineTemplate, const String& shaderPath);
    static void RegisterComputePipeline(const agfx::ComputePipelineCreateInfo& pipelineTemplate, const String& shaderPath);

    static agfx::RenderPipeline* GetPipeline(const String& shaderPath, std::initializer_list<const char*> variants = {});
    static agfx::ComputePipeline* GetComputePipeline(const String& shaderPath, std::initializer_list<const char*> variants = {});

    static void Tick();

private:
    static uint64 EncodeVariantMask(const ParsedShaderSource& source, std::initializer_list<const char*> enabled);
    static bool RealizeVariant(agfx::Device& device, const RegisteredPipeline& owner, uint64 variantMask,
        const ParsedShaderSource& source, CompiledVariant& outVariant);
    static bool RealizeFromBytecode(agfx::Device& device, const RegisteredPipeline& owner, const PendingCompileResult& compiled,
        CompiledVariant& outVariant);
    static void WatcherThreadMain();
    static void RegisterDependencies(const String& shaderPath, const ParsedShaderSource& source);

    static agfx::Device* s_Device;
    static Renderer* s_Renderer;

    static std::mutex s_RegistryMutex;
    static TDictionary<String, RegisteredPipeline> s_Pipelines;
    static TDictionary<String, TArray<String>> s_ReverseDeps;

    static std::mutex s_PendingSwapsMutex;
    static TArray<PendingCompileResult> s_PendingSwaps;

    static std::thread s_WatcherThread;
    static std::atomic<bool> s_WatcherRunning;
};
