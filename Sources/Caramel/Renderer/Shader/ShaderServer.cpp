/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:15:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ShaderServer.hpp"
#include "ShaderParser.hpp"
#include "ShaderCompiler.hpp"

#include <Caramel/Core/Logger.hpp>
#include <Caramel/Renderer/Renderer.hpp>

#include <chrono>
#include <filesystem>
#include <utility>

agfx::Device* ShaderServer::s_Device = nullptr;
Renderer* ShaderServer::s_Renderer = nullptr;

std::mutex ShaderServer::s_RegistryMutex;
TDictionary<String, RegisteredPipeline> ShaderServer::s_Pipelines;
TDictionary<String, TArray<String>> ShaderServer::s_ReverseDeps;

std::mutex ShaderServer::s_PendingSwapsMutex;
TArray<PendingCompileResult> ShaderServer::s_PendingSwaps;

std::thread ShaderServer::s_WatcherThread;
std::atomic<bool> ShaderServer::s_WatcherRunning{ false };

namespace {

// Builds the shader modules + pipeline for one variant from already-compiled bytecode. Only ever
// called on the render thread (either synchronously from GetPipeline's cache-miss path, or from
// Tick() after WaitIdle()) -- this is the only code that touches agfx::Device::CreateShaderModule/
// CreateRenderPipeline/CreateComputePipeline.
bool CreatePipelineFromBytecode(agfx::Device& device, const RegisteredPipeline& owner,
    const TDictionary<EShaderStage, TArray<uint8>>& bytecode, const TDictionary<EShaderStage, String>& entryPoints,
    const uint32 groupSize[3], CompiledVariant& outVariant)
{
    CompiledVariant result;
    for (auto& [stage, code] : bytecode) {
        auto entryIt = entryPoints.Find(stage);
        if (entryIt == entryPoints.End())
            continue;

        agfx::ShaderModuleCreateInfo moduleInfo;
        moduleInfo.SetCode(const_cast<uint8_t*>(code.Data()), code.Size())
                  .SetEntryPoint(entryIt->second.CStr())
                  .SetType(ToAgfxShaderModuleType(stage));
        result.Modules[stage] = device.CreateShaderModule(moduleInfo);
    }

    if (owner.Kind == EPipelineKind::Render) {
        agfx::RenderPipelineCreateInfo info = owner.RenderTemplate;
        if (auto it = result.Modules.Find(EShaderStage::Vertex);   it != result.Modules.End()) info.SetVertexShader(it->second.Get());
        if (auto it = result.Modules.Find(EShaderStage::Fragment); it != result.Modules.End()) info.SetFragmentShader(it->second.Get());
        if (auto it = result.Modules.Find(EShaderStage::Task);     it != result.Modules.End()) info.SetTaskShader(it->second.Get(), groupSize[0], groupSize[1], groupSize[2]);
        if (auto it = result.Modules.Find(EShaderStage::Mesh);     it != result.Modules.End()) info.SetMeshShader(it->second.Get(), groupSize[0], groupSize[1], groupSize[2]);

        result.RenderPipeline = device.CreateRenderPipeline(info);
        if (!result.RenderPipeline) {
            CARAMEL_ERROR("ShaderServer: CreateRenderPipeline failed for '{}'", owner.Source.ShaderPath.CStr());
            return false;
        }
    } else {
        agfx::ComputePipelineCreateInfo info = owner.ComputeTemplate;
        if (auto it = result.Modules.Find(EShaderStage::Compute); it != result.Modules.End())
            info.SetShader(it->second.Get(), groupSize[0], groupSize[1], groupSize[2]);

        result.ComputePipeline = device.CreateComputePipeline(info);
        if (!result.ComputePipeline) {
            CARAMEL_ERROR("ShaderServer: CreateComputePipeline failed for '{}'", owner.Source.ShaderPath.CStr());
            return false;
        }
    }

    outVariant = std::move(result);
    return true;
}

constexpr int kPollIntervalMs = 300;
constexpr int kDebounceStablePolls = 2;

struct WatchEntry
{
    std::filesystem::file_time_type LastObservedMTime{};
    std::filesystem::file_time_type CommittedMTime{};
    int StableCount = 0;
    bool Seeded = false;
};

} // namespace

void ShaderServer::Initialize(agfx::Device& device, Renderer& renderer)
{
    s_Device = &device;
    s_Renderer = &renderer;
    s_WatcherRunning = true;
    s_WatcherThread = std::thread(&ShaderServer::WatcherThreadMain);
}

void ShaderServer::Shutdown()
{
    s_WatcherRunning = false;
    if (s_WatcherThread.joinable())
        s_WatcherThread.join();

    std::lock_guard<std::mutex> lock(s_RegistryMutex);
    s_Pipelines.Clear();
    s_ReverseDeps.Clear();
    s_Device = nullptr;
    s_Renderer = nullptr;
}

void ShaderServer::RegisterDependencies(const String& shaderPath, const ParsedShaderSource& source)
{
    for (auto& [file, dependents] : s_ReverseDeps) {
        for (size_t i = 0; i < dependents.Size(); ) {
            if (dependents[i] == shaderPath)
                dependents.Erase(i);
            else
                ++i;
        }
    }

    for (auto& file : source.DependencyFiles) {
        auto& dependents = s_ReverseDeps[file];
        bool exists = false;
        for (auto& d : dependents) {
            if (d == shaderPath) { exists = true; break; }
        }
        if (!exists)
            dependents.PushBack(shaderPath);
    }
}

void ShaderServer::RegisterRenderPipeline(const agfx::RenderPipelineCreateInfo& pipelineTemplate, const String& shaderPath)
{
    auto parsed = ParseShaderFile(shaderPath);
    if (!parsed) {
        CARAMEL_ERROR("ShaderServer: failed to register render pipeline '{}'", shaderPath.CStr());
        return;
    }

    std::lock_guard<std::mutex> lock(s_RegistryMutex);
    RegisteredPipeline entry;
    entry.Kind = EPipelineKind::Render;
    entry.RenderTemplate = pipelineTemplate;
    entry.Source = *parsed;
    s_Pipelines[shaderPath] = std::move(entry);
    RegisterDependencies(shaderPath, *parsed);
}

void ShaderServer::RegisterComputePipeline(const agfx::ComputePipelineCreateInfo& pipelineTemplate, const String& shaderPath)
{
    auto parsed = ParseShaderFile(shaderPath);
    if (!parsed) {
        CARAMEL_ERROR("ShaderServer: failed to register compute pipeline '{}'", shaderPath.CStr());
        return;
    }

    std::lock_guard<std::mutex> lock(s_RegistryMutex);
    RegisteredPipeline entry;
    entry.Kind = EPipelineKind::Compute;
    entry.ComputeTemplate = pipelineTemplate;
    entry.Source = *parsed;
    s_Pipelines[shaderPath] = std::move(entry);
    RegisterDependencies(shaderPath, *parsed);
}

uint64 ShaderServer::EncodeVariantMask(const ParsedShaderSource& source, std::initializer_list<const char*> enabled)
{
    uint64 mask = 0;
    for (const char* name : enabled) {
        bool found = false;
        for (uint64 i = 0; i < source.Variants.Size(); ++i) {
            if (source.Variants[i] == name) {
                mask |= (1ull << i);
                found = true;
                break;
            }
        }
        if (!found)
            CARAMEL_WARN("ShaderServer: '{}' is not a declared variant of '{}', ignoring", name, source.ShaderPath.CStr());
    }
    return mask;
}

bool ShaderServer::RealizeVariant(agfx::Device& device, const RegisteredPipeline& owner, uint64 variantMask,
    const ParsedShaderSource& source, CompiledVariant& outVariant)
{
    if (source.StageEntryPoints.Size() == 0) {
        CARAMEL_ERROR("ShaderServer: '{}' declares no #pragma vertex/pixel/compute/task/mesh entry points", source.ShaderPath.CStr());
        return false;
    }

    TDictionary<EShaderStage, TArray<uint8>> bytecode;
    uint32 groupSize[3] = { 1, 1, 1 };
    for (auto& [stage, entry] : source.StageEntryPoints) {
        TArray<uint8> stageBytecode;
        uint32 stageGroupSize[3] = { 1, 1, 1 };
        if (!CompileStage(source, stage, entry, variantMask, stageBytecode, stageGroupSize))
            return false;

        bytecode[stage] = std::move(stageBytecode);
        if (stage == EShaderStage::Compute || stage == EShaderStage::Mesh || stage == EShaderStage::Task) {
            groupSize[0] = stageGroupSize[0];
            groupSize[1] = stageGroupSize[1];
            groupSize[2] = stageGroupSize[2];
        }
    }

    return CreatePipelineFromBytecode(device, owner, bytecode, source.StageEntryPoints, groupSize, outVariant);
}

bool ShaderServer::RealizeFromBytecode(agfx::Device& device, const RegisteredPipeline& owner,
    const PendingCompileResult& compiled, CompiledVariant& outVariant)
{
    return CreatePipelineFromBytecode(device, owner, compiled.Bytecode, compiled.NewSource.StageEntryPoints, compiled.GroupSize, outVariant);
}

agfx::RenderPipeline* ShaderServer::GetPipeline(const String& shaderPath, std::initializer_list<const char*> variants)
{
    uint64 mask = 0;
    {
        std::lock_guard<std::mutex> lock(s_RegistryMutex);
        auto it = s_Pipelines.Find(shaderPath);
        if (it == s_Pipelines.End() || it->second.Kind != EPipelineKind::Render) {
            CARAMEL_ERROR("ShaderServer::GetPipeline: '{}' was never registered as a render pipeline", shaderPath.CStr());
            return nullptr;
        }

        mask = EncodeVariantMask(it->second.Source, variants);
        auto variantIt = it->second.VariantCache.Find(mask);
        if (variantIt != it->second.VariantCache.End())
            return &variantIt->second.RenderPipeline;
    }

    RegisteredPipeline snapshot;
    {
        std::lock_guard<std::mutex> lock(s_RegistryMutex);
        auto it = s_Pipelines.Find(shaderPath);
        if (it == s_Pipelines.End())
            return nullptr;
        snapshot.Kind = EPipelineKind::Render;
        snapshot.RenderTemplate = it->second.RenderTemplate;
        snapshot.Source = it->second.Source;
    }

    CompiledVariant compiled;
    if (!RealizeVariant(*s_Device, snapshot, mask, snapshot.Source, compiled))
        return nullptr;

    std::lock_guard<std::mutex> lock(s_RegistryMutex);
    auto& variantCache = s_Pipelines[shaderPath].VariantCache;
    variantCache[mask] = std::move(compiled);
    return &variantCache[mask].RenderPipeline;
}

agfx::ComputePipeline* ShaderServer::GetComputePipeline(const String& shaderPath, std::initializer_list<const char*> variants)
{
    uint64 mask = 0;
    {
        std::lock_guard<std::mutex> lock(s_RegistryMutex);
        auto it = s_Pipelines.Find(shaderPath);
        if (it == s_Pipelines.End() || it->second.Kind != EPipelineKind::Compute) {
            CARAMEL_ERROR("ShaderServer::GetComputePipeline: '{}' was never registered as a compute pipeline", shaderPath.CStr());
            return nullptr;
        }

        mask = EncodeVariantMask(it->second.Source, variants);
        auto variantIt = it->second.VariantCache.Find(mask);
        if (variantIt != it->second.VariantCache.End())
            return &variantIt->second.ComputePipeline;
    }

    RegisteredPipeline snapshot;
    {
        std::lock_guard<std::mutex> lock(s_RegistryMutex);
        auto it = s_Pipelines.Find(shaderPath);
        if (it == s_Pipelines.End())
            return nullptr;
        snapshot.Kind = EPipelineKind::Compute;
        snapshot.ComputeTemplate = it->second.ComputeTemplate;
        snapshot.Source = it->second.Source;
    }

    CompiledVariant compiled;
    if (!RealizeVariant(*s_Device, snapshot, mask, snapshot.Source, compiled))
        return nullptr;

    std::lock_guard<std::mutex> lock(s_RegistryMutex);
    auto& variantCache = s_Pipelines[shaderPath].VariantCache;
    variantCache[mask] = std::move(compiled);
    return &variantCache[mask].ComputePipeline;
}

void ShaderServer::Tick()
{
    TArray<PendingCompileResult> swaps;
    {
        std::lock_guard<std::mutex> lock(s_PendingSwapsMutex);
        if (s_PendingSwaps.IsEmpty())
            return;
        swaps = std::move(s_PendingSwaps);
        s_PendingSwaps.Clear();
    }

    s_Renderer->WaitIdle();

    std::lock_guard<std::mutex> lock(s_RegistryMutex);
    for (auto& swap : swaps) {
        auto it = s_Pipelines.Find(swap.ShaderPath);
        if (it == s_Pipelines.End())
            continue;

        CompiledVariant realized;
        if (!RealizeFromBytecode(*s_Device, it->second, swap, realized)) {
            CARAMEL_ERROR("ShaderServer: failed to realize hot-reloaded pipeline for '{}', keeping previous pipeline", swap.ShaderPath.CStr());
            continue;
        }

        it->second.VariantCache[swap.VariantMask] = std::move(realized);
        it->second.Source = swap.NewSource;
        CARAMEL_INFO("ShaderServer: hot-reloaded '{}'", swap.ShaderPath.CStr());
    }
}

void ShaderServer::WatcherThreadMain()
{
    // Watcher-thread-only state -- never touched by the render thread, so no locking needed here.
    TDictionary<String, WatchEntry> watchState;

    while (s_WatcherRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
        if (!s_WatcherRunning)
            break;

        TArray<String> depFiles;
        {
            std::lock_guard<std::mutex> lock(s_RegistryMutex);
            for (auto& [file, dependents] : s_ReverseDeps) {
                if (!dependents.IsEmpty())
                    depFiles.PushBack(file);
            }
        }

        TArray<String> changedFiles;
        for (auto& file : depFiles) {
            std::error_code ec;
            auto mtime = std::filesystem::last_write_time(file.CStr(), ec);
            if (ec)
                continue;

            auto& entry = watchState[file];
            if (!entry.Seeded) {
                entry.Seeded = true;
                entry.LastObservedMTime = mtime;
                entry.CommittedMTime = mtime;
                entry.StableCount = 0;
                continue;
            }

            if (mtime != entry.LastObservedMTime) {
                entry.LastObservedMTime = mtime;
                entry.StableCount = 1;
            } else {
                entry.StableCount++;
            }

            if (entry.StableCount >= kDebounceStablePolls && mtime != entry.CommittedMTime) {
                entry.CommittedMTime = mtime;
                changedFiles.PushBack(file);
            }
        }

        if (changedFiles.IsEmpty())
            continue;

        struct Work
        {
            String ShaderPath;
            EPipelineKind Kind;
            TArray<uint64> CachedMasks;
        };
        TArray<Work> work;
        {
            std::lock_guard<std::mutex> lock(s_RegistryMutex);
            TDictionary<String, bool> seen;
            for (auto& file : changedFiles) {
                auto depIt = s_ReverseDeps.Find(file);
                if (depIt == s_ReverseDeps.End())
                    continue;

                for (auto& shaderPath : depIt->second) {
                    if (seen.Contains(shaderPath))
                        continue;
                    seen.Insert(shaderPath, true);

                    auto pipeIt = s_Pipelines.Find(shaderPath);
                    if (pipeIt == s_Pipelines.End())
                        continue;

                    Work w;
                    w.ShaderPath = shaderPath;
                    w.Kind = pipeIt->second.Kind;
                    for (auto& [mask, variant] : pipeIt->second.VariantCache)
                        w.CachedMasks.PushBack(mask);
                    work.PushBack(std::move(w));
                }
            }
        }

        for (auto& w : work) {
            if (w.CachedMasks.IsEmpty())
                continue; // never actually requested via GetPipeline yet -- nothing live to hot-swap

            auto freshSource = ParseShaderFile(w.ShaderPath);
            if (!freshSource) {
                CARAMEL_ERROR("ShaderServer: hot-reload reparse failed for '{}', keeping previous pipeline", w.ShaderPath.CStr());
                continue;
            }

            for (uint64 mask : w.CachedMasks) {
                PendingCompileResult result;
                result.ShaderPath = w.ShaderPath;
                result.VariantMask = mask;
                result.NewSource = *freshSource;

                bool ok = freshSource->StageEntryPoints.Size() > 0;
                uint32 groupSize[3] = { 1, 1, 1 };
                for (auto& [stage, entry] : freshSource->StageEntryPoints) {
                    TArray<uint8> bytecode;
                    uint32 stageGroupSize[3] = { 1, 1, 1 };
                    if (!CompileStage(*freshSource, stage, entry, mask, bytecode, stageGroupSize)) {
                        ok = false;
                        break;
                    }
                    result.Bytecode[stage] = std::move(bytecode);
                    if (stage == EShaderStage::Compute || stage == EShaderStage::Mesh || stage == EShaderStage::Task) {
                        groupSize[0] = stageGroupSize[0];
                        groupSize[1] = stageGroupSize[1];
                        groupSize[2] = stageGroupSize[2];
                    }
                }

                if (!ok) {
                    CARAMEL_ERROR("ShaderServer: hot-reload compile failed for '{}' (variant mask {}), keeping previous pipeline", w.ShaderPath.CStr(), mask);
                    continue;
                }

                result.GroupSize[0] = groupSize[0];
                result.GroupSize[1] = groupSize[1];
                result.GroupSize[2] = groupSize[2];

                std::lock_guard<std::mutex> lock(s_PendingSwapsMutex);
                s_PendingSwaps.PushBack(std::move(result));
            }

            // Refresh the dependency graph in case #include lines changed (files added/removed) --
            // harmless to do even if every variant above failed to compile, since this only affects
            // which files are watched, not any live pipeline.
            std::lock_guard<std::mutex> lock(s_RegistryMutex);
            if (s_Pipelines.Contains(w.ShaderPath))
                RegisterDependencies(w.ShaderPath, *freshSource);
        }
    }
}
