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

// Compiles and caches AGFX pipelines from HLSL shader files that declare their entry points and
// variant toggles via `#pragma vertex/pixel/compute/task/mesh <Entry>` / `#pragma variant <Name>`,
// and automatically recompiles + hot-swaps them when the source (or anything it #includes)
// changes on disk.
//
// Usage:
//   ShaderServer::RegisterRenderPipeline(pipelineTemplate, "Content/Shaders/Foo.hlsl"); // once, at setup
//   ...
//   agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline("Content/Shaders/Foo.hlsl", { "SOME_VARIANT" }); // every frame
//
// `pipelineTemplate` must leave its shader-module fields (vertexShader/fragmentShader/etc.)
// unset -- ShaderServer fills those in per requested variant combination.
class ShaderServer
{
public:
    // Starts the background watcher thread. Call once, after `device` is fully constructed (end
    // of Renderer's constructor). Neither `device` nor `renderer` may be destroyed before Shutdown().
    static void Initialize(agfx::Device& device, Renderer& renderer);

    // Stops and joins the watcher thread, then destroys every cached shader module/pipeline.
    // Must be called while the device is still alive -- call as the first line of ~Renderer().
    static void Shutdown();

    static void RegisterRenderPipeline(const agfx::RenderPipelineCreateInfo& pipelineTemplate, const String& shaderPath);
    static void RegisterComputePipeline(const agfx::ComputePipelineCreateInfo& pipelineTemplate, const String& shaderPath);

    // Returns the currently-active pipeline for this variant combination, lazily compiling and
    // caching it on first use (a synchronous, blocking compile -- acceptable since it only stalls
    // the first frame a new variant combination is requested). Returns nullptr (after logging via
    // CARAMEL_ERROR) if `shaderPath` was never registered, was registered as the other kind, or
    // the very first compile for this variant combination failed.
    static agfx::RenderPipeline* GetPipeline(const String& shaderPath, std::initializer_list<const char*> variants = {});
    static agfx::ComputePipeline* GetComputePipeline(const String& shaderPath, std::initializer_list<const char*> variants = {});

    // Call exactly once per frame, from Renderer::Render() right after the per-slot fence wait
    // (the one safe point in the frame where it's known no in-flight command buffer references a
    // pipeline that's about to be replaced -- assuming Renderer::WaitIdle() is also called here
    // when there's an actual pending swap, since a single frame slot's fence wait alone isn't
    // sufficient to guarantee no *other* slot still has the old pipeline bound).
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
