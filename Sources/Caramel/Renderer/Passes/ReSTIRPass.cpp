/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ReSTIRPass.hpp"

#include <Caramel/Renderer/Passes/DeferredTargets.hpp>
#include <Caramel/Renderer/SceneRenderer.hpp>
#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <Caramel/Core/CVar.hpp>
#include <Caramel/Scene/GPUScene.hpp>

namespace
{
    // Off by default: the reservoir stages are still empty, so this currently buys a slower path to
    // the same image, minus material schemes. Flip it on to work on them.
    CVar cv_ReSTIR("scene.restir", false, "ReSTIR DI", "Scene Renderer",
        "Shade through the ReSTIR direct-lighting path instead of the per-scheme visibility dispatch. Bypasses material schemes: every surface shades as DefaultPBR while this is on");

    // The scheme path reads its ambient term out of a material's scheme parameters
    // (DefaultPBR.json). ReSTIR has no scheme to read one from, so it gets a global -- without it a
    // scene with no lights is pure black rather than merely dark.
    CVar cv_ReSTIRAmbient("scene.restir_ambient", 0.03f, 0.0f, 1.0f, "ReSTIR Ambient", "Scene Renderer",
        "Flat ambient term for the ReSTIR path, which has no scheme parameters to read one from");

    constexpr const char* kInitialSampleShaderPath = "Content/Shaders/ReSTIRInitialSample.hlsl";
    constexpr const char* kTemporalReuseShaderPath = "Content/Shaders/ReSTIRTemporalReuse.hlsl";
    constexpr const char* kSpatialReuseShaderPath  = "Content/Shaders/ReSTIRSpatialReuse.hlsl";
    constexpr const char* kShadeShaderPath         = "Content/Shaders/ReSTIRShade.hlsl";

    // Mirrors kReSTIRGroupSize in Content/Shaders/Common/ReSTIR.hlsli. Every stage is one thread per
    // pixel over an 8x8 tile, unlike the scheme kernels' 1D dispatch over a compacted pixel list --
    // the reuse stages need the 2D neighbourhood, which a compacted list destroys.
    constexpr uint32 kReSTIRGroupSize = 8;

    // Mirrors ReSTIRPushConstants in Content/Shaders/Common/ReSTIR.hlsli. All scalars, deliberately:
    // push constants pack by cbuffer rules, where a vector may not straddle a 16-byte boundary, so
    // inserting a field ahead of one silently shifts every field after it (same warning as
    // DeferredShading.hlsli and ClusterCull.hlsli).
    struct ReSTIRPushConstants
    {
        uint32 rFrameConstants;
        uint32 rInstanceBuffer;
        uint32 rMaterialBuffer;

        uint32 rVisibility;
        uint32 rDepth;
        uint32 rAlbedo;
        uint32 rNormal;
        uint32 rMetallicRoughness;
        uint32 rEmissive;
        uint32 rMotion;

        uint32 rOutput;

        uint32 rLightBuffer;
        uint32 uLightCount;

        uint32 rClusterLights;
        uint32 rLightCull;
        float32 fClusterSliceScale;
        float32 fClusterSliceBias;
        uint32 uClusteringEnabled;

        // Both 0 until the reservoir buffers exist -- the stages guard on them rather than reading a
        // handle that describes nothing, exactly as the shading kernels guard on uClusteringEnabled.
        uint32 rReservoirsIn;
        uint32 rReservoirsOut;

        uint32 uWidth;
        uint32 uHeight;
        uint32 uFrameNumber;
        float32 fAmbient;
    };
    static_assert(sizeof(ReSTIRPushConstants) <= 128, "Push constants are capped at 128 bytes by the root signature");
}

ReSTIRPass::ReSTIRPass(agfx::Device& device)
    : m_Device(&device)
{
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("ReSTIR Initial Sample Pipeline"), kInitialSampleShaderPath);
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("ReSTIR Temporal Reuse Pipeline"), kTemporalReuseShaderPath);
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("ReSTIR Spatial Reuse Pipeline"), kSpatialReuseShaderPath);
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("ReSTIR Shade Pipeline"), kShadeShaderPath);

    // No resources yet. The reservoir buffers belong here, created once and resized never -- they are
    // per-pixel, so they grow in Resize(), not per frame. Two of them, ping-ponged by ctx.frameIndex
    // parity, since temporal reuse reads the previous frame's while writing this frame's.
}

bool ReSTIRPass::IsShadingActive()
{
    return *cv_ReSTIR.AsBoolPtr();
}

void ReSTIRPass::BeginFrame(FrameContext& ctx)
{
    ++m_FrameNumber;
}

bool ReSTIRPass::Enabled(const FrameContext& ctx) const
{
    // The same two guards ClassifyMaterials applies internally, hoisted to the pass level: a gbuffer
    // debug view already owns the lighting target and shading would only overwrite what is being
    // inspected, and an empty scene has nothing to shade.
    if (!IsShadingActive() || SceneRenderer::IsGBufferDebugActive())
        return false;
    return ctx.gpuScene && ctx.gpuScene->GetInstanceCount() > 0;
}

void ReSTIRPass::Register(RenderGraph& graph, FrameContext& ctx)
{
    graph.AddPass("ReSTIR DI",
        [&](RGPassBuilder& builder) {
            for (uint32 i = 0; i < kGBufferTextureCount; ++i)
                builder.ReadTexture(ctx.gbuffer[i], agfx::ResourceState::NonPixelShaderResource);
            builder.ReadTexture(ctx.visibility, agfx::ResourceState::NonPixelShaderResource);
            builder.ReadTexture(ctx.depth, agfx::ResourceState::NonPixelShaderResource);
            builder.WriteTexture(ctx.sceneLighting, agfx::ResourceState::UnorderedAccess);
            // No AlwaysExecute(): unlike Material Classify, every resource this pass touches is
            // graph-visible. That stops being true the moment the reservoir buffers land, since the
            // graph cannot model a raw agfx buffer -- add it then, alongside their manual barriers.
        },
        [this, &ctx](agfx::CommandBuffer& cmd, RGResolveContext& rc) {
            // Null while the shader watcher recompiles. Nothing here has to run regardless -- there
            // is no indirect bundle whose state must be transitioned, as in Material Classify -- so
            // each stage simply guards, and a missing Shade leaves the previous frame's lighting.
            agfx::ComputePipeline* initialPipeline = ShaderServer::GetComputePipeline(kInitialSampleShaderPath, {});
            agfx::ComputePipeline* temporalPipeline = ShaderServer::GetComputePipeline(kTemporalReuseShaderPath, {});
            agfx::ComputePipeline* spatialPipeline = ShaderServer::GetComputePipeline(kSpatialReuseShaderPath, {});
            agfx::ComputePipeline* shadePipeline = ShaderServer::GetComputePipeline(kShadeShaderPath, {});

            GPUScene& gpuScene = *ctx.gpuScene;
            DeferredTargets targets = ResolveDeferredTargets(rc, ctx);

            ReSTIRPushConstants pc{};
            pc.rFrameConstants = ctx.frameConstantsHandle;
            pc.rInstanceBuffer = (uint32)gpuScene.GetInstanceBufferView(ctx.frameIndex).GetHandle();
            pc.rMaterialBuffer = (uint32)gpuScene.GetMaterialBufferView(ctx.frameIndex).GetHandle();

            pc.rVisibility = targets.visibilityHandle;
            pc.rDepth = targets.depthHandle;
            pc.rAlbedo = targets.albedoHandle;
            pc.rNormal = targets.normalHandle;
            pc.rMetallicRoughness = targets.metallicRoughnessHandle;
            pc.rEmissive = targets.emissiveHandle;
            pc.rMotion = targets.motionHandle;
            pc.rOutput = targets.sceneLightingUAVHandle;

            // Null on a frame slot that has never held a light. uLightCount covers the other case,
            // where the view outlives the lights it described -- see GPUScene::GetLightCount.
            agfx::BufferView& lightView = gpuScene.GetLightBufferView(ctx.frameIndex);
            pc.rLightBuffer = lightView ? (uint32)lightView.GetHandle() : 0u;
            pc.uLightCount = gpuScene.GetLightCount();

            // Null before ClusteredLightPass has run its first BeginFrame. uClusteringEnabled staying
            // 0 is what makes that safe: the candidate set falls back to every light in the scene,
            // which is the same fallback the scheme path takes.
            if (ctx.clusters)
            {
                pc.rClusterLights = ctx.clusters->clusterLightsHandle;
                pc.rLightCull = ctx.clusters->lightCullHandle;
                pc.fClusterSliceScale = ctx.clusters->sliceScale;
                pc.fClusterSliceBias = ctx.clusters->sliceBias;
                pc.uClusteringEnabled = ctx.clusters->enabled;
            }

            // TODO: point these at the ping-ponged reservoir buffers once they exist. 0 until then,
            // which every stage checks before touching a reservoir.
            pc.rReservoirsIn = 0;
            pc.rReservoirsOut = 0;

            pc.uWidth = ctx.width;
            pc.uHeight = ctx.height;
            pc.uFrameNumber = m_FrameNumber;
            pc.fAmbient = *cv_ReSTIRAmbient.AsFloatPtr();

            uint32 groupsX = (ctx.width + kReSTIRGroupSize - 1) / kReSTIRGroupSize;
            uint32 groupsY = (ctx.height + kReSTIRGroupSize - 1) / kReSTIRGroupSize;

            agfx::ComputePass computePass = cmd.BeginComputePass("ReSTIR DI");

            auto stage = [&](agfx::ComputePipeline* pipeline) {
                if (!pipeline)
                    return;
                computePass.SetPipeline(*pipeline);
                computePass.PushConstants(pc);
                computePass.Dispatch(groupsX, groupsY, 1);
            };

            // Every stage reads what the stage before it wrote, across the whole screen and not just
            // within a group -- there is no ordering between groups without a barrier.
            //
            // !! Each of these stage boundaries needs computePass.BufferUAVBarrier(<reservoir buffer>)
            // !! once the buffers exist. Omitting one will not fail loudly: the reuse stages will read
            // !! a mix of this frame's and last frame's reservoirs and it will look like a sampling
            // !! bug, not a synchronisation one.
            stage(initialPipeline);
            stage(temporalPipeline);
            stage(spatialPipeline);
            stage(shadePipeline);
        });
}
