/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ClusteredLightPass.hpp"

#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <Caramel/Core/CVar.hpp>
#include <Caramel/Core/Logger.hpp>
#include <Caramel/Scene/GPUScene.hpp>

#include <cmath>
#include <cstring>

#if defined(CARAMEL_WINDOWS)
    #undef MemoryBarrier
#endif

namespace
{
    // Off: the grid is still built, but the shading kernels go back to walking every light in the
    // scene. That is the ground truth to diff against -- the two paths must produce an identical
    // image, because LightEvaluate's windowed falloff reaches exactly zero at the light's range, so
    // a light this culling drops contributes exactly nothing.
    CVar cv_ClusteredLights("scene.clustered_lights", true, "Clustered Light Culling", "Scene Renderer",
        "Narrow each pixel's light loop to the lights binned into its cluster. Off walks every light, which is the ground truth to compare against");

    constexpr const char* kLightCullFrustumShaderPath = "Content/Shaders/LightCullFrustum.hlsl";
    constexpr const char* kBuildLightClustersShaderPath = "Content/Shaders/BuildLightClusters.hlsl";
    constexpr const char* kCullLightClustersShaderPath = "Content/Shaders/CullLightClusters.hlsl";

    // All of these mirror Content/Shaders/Common/ClusteredLights.hlsli and must not drift from it.
    constexpr uint32 kClusterGroupSize = 64;
    constexpr uint32 kClusterLightWords = kClusterCount + kClusterCount * kMaxLightsPerCluster;
    constexpr uint32 kLightCullWords = 4 + kMaxDirectionalLights;

    // Word indices into m_LightCull, i.e. kLightCull*Base / 4.
    constexpr uint32 kDirectionalCountWord = 0;
    constexpr uint32 kLocalCountWord = 1;
    constexpr uint32 kOverflowWord = 2;

    // Only the three counters are reset per frame; the directional index list past them is fully
    // rewritten by whatever the counter allows.
    constexpr uint32 kResetWords = 3;

    // Mirrors ClusterPushConstants in Content/Shaders/Common/ClusterCull.hlsli. All scalars,
    // deliberately -- see the comment there.
    struct ClusterPushConstants
    {
        uint32 rFrameConstants;
        uint32 rLightBuffer;
        uint32 uLightCount;
        uint32 rLightCull;
        uint32 rVisibleLights;
        uint32 rClusterAABBs;
        uint32 rClusterLights;
        float32 fSliceScale;
        float32 fSliceBias;
        uint32 uMaxVisibleLights;
    };
    static_assert(sizeof(ClusterPushConstants) <= 128, "Push constants are capped at 128 bytes by the root signature");
}

ClusteredLightPass::ClusteredLightPass(agfx::Device& device)
    : m_Device(&device)
{
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("Light Frustum Cull Pipeline"), kLightCullFrustumShaderPath);
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("Build Light Clusters Pipeline"), kBuildLightClustersShaderPath);
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("Cull Light Clusters Pipeline"), kCullLightClustersShaderPath);

    // The grid is a fixed 16x9x24, so every buffer below is created exactly once and Resize() is a
    // no-op -- that resolution independence is the whole reason for a fixed grid over pixel tiles.
    agfx::BufferCreateInfo aabbInfo;
    aabbInfo.SetSize((uint64)kClusterCount * sizeof(float32) * 8)   // float4 min + float4 max
            .SetStride(sizeof(float32) * 8)
            .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
            .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_ClusterAABBs = m_Device->CreateBuffer(aabbInfo);
    m_ClusterAABBs.SetName("Cluster AABBs");

    agfx::BufferViewCreateInfo aabbViewInfo;
    aabbViewInfo.SetBuffer(m_ClusterAABBs.Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(true);
    m_ClusterAABBsView = m_Device->CreateBufferView(aabbViewInfo);

    agfx::BufferCreateInfo lightsInfo;
    lightsInfo.SetSize((uint64)kClusterLightWords * sizeof(uint32))
              .SetStride(sizeof(uint32))
              .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
              .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_ClusterLights = m_Device->CreateBuffer(lightsInfo);
    m_ClusterLights.SetName("Cluster Light Lists");

    agfx::BufferViewCreateInfo lightsViewInfo;
    lightsViewInfo.SetBuffer(m_ClusterLights.Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_ClusterLightsView = m_Device->CreateBufferView(lightsViewInfo);

    agfx::BufferCreateInfo cullInfo;
    cullInfo.SetSize(kLightCullWords * sizeof(uint32))
            .SetStride(sizeof(uint32))
            .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
            .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_LightCull = m_Device->CreateBuffer(cullInfo);
    m_LightCull.SetName("Light Cull Counters");

    agfx::BufferViewCreateInfo cullViewInfo;
    cullViewInfo.SetBuffer(m_LightCull.Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_LightCullView = m_Device->CreateBufferView(cullViewInfo);

    for (uint32 i = 0; i < FRAMES_IN_FLIGHT; ++i)
    {
        agfx::BufferCreateInfo readbackInfo;
        readbackInfo.SetSize(kResetWords * sizeof(uint32))
                    .SetStride(sizeof(uint32))
                    .SetUsage(agfx::BufferUsage::ShaderRead)
                    .SetMemoryType(agfx::BufferMemoryType::GPUToCPU);
        m_CounterReadback[i] = m_Device->CreateBuffer(readbackInfo);
        m_CounterReadback[i].SetName("Light Cull Counter Readback");

        // Zero it up front: the first FRAMES_IN_FLIGHT frames read a slot nothing has written yet.
        agfx::MappedBuffer mapped(m_CounterReadback[i]);
        std::memset(mapped.Get(), 0, kResetWords * sizeof(uint32));
    }

    agfx::BufferCreateInfo zeroInfo;
    zeroInfo.SetSize(kResetWords * sizeof(uint32))
            .SetStride(sizeof(uint32))
            .SetUsage(agfx::BufferUsage::ShaderRead)
            .SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
    m_ZeroBuffer = m_Device->CreateBuffer(zeroInfo);
    m_ZeroBuffer.SetName("Light Cull Zero Buffer");
    {
        agfx::MappedBuffer mapped(m_ZeroBuffer);
        std::memset(mapped.Get(), 0, kResetWords * sizeof(uint32));
    }

    m_Device->MakeResourcesResident();
}

void ClusteredLightPass::EnsureVisibleLightCapacity(uint32 lightCount)
{
    if (lightCount <= m_VisibleLightCapacity)
        return;

    // Overshoot so a scene that gains lights one at a time does not reallocate every frame, the same
    // growth rule as StreamBuffer::Reserve and EnsureOpaqueCapacity.
    uint32 newCapacity = lightCount + lightCount / 2 + 64;

    agfx::BufferCreateInfo info;
    info.SetSize((uint64)newCapacity * sizeof(uint32))
        .SetStride(sizeof(uint32))
        .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
        .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_VisibleLights = m_Device->CreateBuffer(info);
    m_VisibleLights.SetName("Frustum Visible Lights");

    agfx::BufferViewCreateInfo viewInfo;
    viewInfo.SetBuffer(m_VisibleLights.Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_VisibleLightsView = m_Device->CreateBufferView(viewInfo);

    m_VisibleLightCapacity = newCapacity;
    m_Device->MakeResourcesResident();
}

void ClusteredLightPass::ReportOverflow(uint32 frameIndex)
{
    uint32 counters[kResetWords] = {};
    {
        agfx::MappedBuffer mapped(m_CounterReadback[frameIndex]);
        std::memcpy(counters, mapped.Get(), sizeof(counters));
    }

    if (counters[kDirectionalCountWord] > kMaxDirectionalLights && !m_WarnedDirectionalOverflow)
    {
        CARAMEL_ERROR("ClusteredLightPass: {} directional lights in the scene but only {} fit the unclustered list; the rest are not shaded",
                      counters[kDirectionalCountWord], kMaxDirectionalLights);
        m_WarnedDirectionalOverflow = true;
    }

    if (counters[kOverflowWord] > kMaxLightsPerCluster && !m_WarnedClusterOverflow)
    {
        CARAMEL_ERROR("ClusteredLightPass: a cluster wanted {} lights but only {} fit; the excess is dropped. Raise kMaxLightsPerCluster or shorten light ranges (scene.gbuffer_debug 10 shows where)",
                      counters[kOverflowWord], kMaxLightsPerCluster);
        m_WarnedClusterOverflow = true;
    }
}

void ClusteredLightPass::BeginFrame(FrameContext& ctx)
{
    // Reading the slot *before* this frame's copy overwrites it. The fence wait at the top of
    // Renderer::Render already proved this slot's GPU work has retired, so no extra sync is needed.
    ReportOverflow(ctx.frameIndex);

    m_LightCount = ctx.gpuScene ? ctx.gpuScene->GetLightCount() : 0;
    EnsureVisibleLightCapacity(m_LightCount);

    // slice = log2(viewZ) * scale + bias, mapping [near, far] onto [0, kClusterGridZ]. Exponential
    // rather than linear so slices are thin close to the camera, where a cluster's world-space
    // footprint is small and a light covers few of them, and thick far away where the opposite holds.
    float32 nearPlane = ctx.camera ? ctx.camera->GetNearPlane() : 0.1f;
    float32 farPlane = ctx.camera ? ctx.camera->GetFarPlane() : 500.0f;
    float32 logRatio = std::log2(farPlane / nearPlane);

    m_Resources.sliceScale = logRatio > 0.0f ? (float32)kClusterGridZ / logRatio : 0.0f;
    m_Resources.sliceBias = -m_Resources.sliceScale * std::log2(nearPlane);
    m_Resources.clusterLightsHandle = (uint32)m_ClusterLightsView.GetHandle();
    m_Resources.lightCullHandle = (uint32)m_LightCullView.GetHandle();
    m_Resources.enabled = *cv_ClusteredLights.AsBoolPtr() ? 1u : 0u;

    ctx.clusters = &m_Resources;
}

void ClusteredLightPass::Register(RenderGraph& graph, FrameContext& ctx)
{
    graph.AddPass("Cluster Lights",
        [&](RGPassBuilder& builder) {
            // Everything this pass touches is a raw agfx buffer with its own manual barriers, which
            // the graph cannot see -- same situation as Material Classify.
            builder.AlwaysExecute();
        },
        [this, &ctx](agfx::CommandBuffer& cmd, RGResolveContext&) {
            // Null while the shader watcher recompiles. Unlike the classification passes there is no
            // indirect bundle whose state has to be transitioned regardless, so each dispatch simply
            // guards: a missing frustum pass leaves the counters at zero and a missing cluster pass
            // leaves every count at zero, both of which mean "no lights", never stale lists.
            agfx::ComputePipeline* frustumPipeline = ShaderServer::GetComputePipeline(kLightCullFrustumShaderPath, {});
            agfx::ComputePipeline* buildPipeline = ShaderServer::GetComputePipeline(kBuildLightClustersShaderPath, {});
            agfx::ComputePipeline* cullPipeline = ShaderServer::GetComputePipeline(kCullLightClustersShaderPath, {});

            GPUScene& gpuScene = *ctx.gpuScene;

            ClusterPushConstants pc{};
            pc.rFrameConstants = ctx.frameConstantsHandle;
            // Null on a frame slot that has never held a light. uLightCount covers the other case,
            // where the view outlives the lights it described -- see GPUScene::GetLightCount.
            agfx::BufferView& lightView = gpuScene.GetLightBufferView(ctx.frameIndex);
            pc.rLightBuffer = lightView ? (uint32)lightView.GetHandle() : 0u;
            pc.uLightCount = m_LightCount;
            pc.rLightCull = (uint32)m_LightCullView.GetHandle();
            pc.rVisibleLights = m_VisibleLightsView ? (uint32)m_VisibleLightsView.GetHandle() : 0u;
            pc.rClusterAABBs = (uint32)m_ClusterAABBsView.GetHandle();
            pc.rClusterLights = (uint32)m_ClusterLightsView.GetHandle();
            pc.fSliceScale = m_Resources.sliceScale;
            pc.fSliceBias = m_Resources.sliceBias;
            pc.uMaxVisibleLights = m_VisibleLightCapacity;

            {
                agfx::ComputePass computePass = cmd.BeginComputePass("Cluster Lights");

                // The counters accumulate through InterlockedAdd, so they have to start at zero every
                // frame. The previous frame left the buffer in UnorderedAccess, hence the transition
                // around the copy -- same shape as the HZB counter reset.
                cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::CopyDest);
                computePass.CopyBufferToBuffer(m_ZeroBuffer, m_LightCull, 0, 0, kResetWords * sizeof(uint32));
                cmd.MemoryBarrier(agfx::ResourceState::CopyDest, agfx::ResourceState::UnorderedAccess);

                if (frustumPipeline && m_LightCount > 0)
                {
                    computePass.SetPipeline(*frustumPipeline);
                    computePass.PushConstants(pc);
                    computePass.Dispatch((m_LightCount + kClusterGroupSize - 1) / kClusterGroupSize, 1, 1);
                }

                // The grid depends only on the projection, so this is independent of the pass above
                // and could run first; it is second so the two counter transitions stay adjacent.
                if (buildPipeline)
                {
                    computePass.SetPipeline(*buildPipeline);
                    computePass.PushConstants(pc);
                    computePass.Dispatch((kClusterCount + kClusterGroupSize - 1) / kClusterGroupSize, 1, 1);
                }

                computePass.BufferUAVBarrier(m_LightCull);
                computePass.BufferUAVBarrier(m_ClusterAABBs);
                computePass.BufferUAVBarrier(m_ClusterLights);
                if (m_VisibleLights)
                    computePass.BufferUAVBarrier(m_VisibleLights);

                // One group per cluster: the group's threads stride the visible-light list together
                // and batch their appends through groupshared memory.
                if (cullPipeline && m_LightCount > 0)
                {
                    computePass.SetPipeline(*cullPipeline);
                    computePass.PushConstants(pc);
                    computePass.Dispatch(kClusterCount, 1, 1);
                }

                computePass.BufferUAVBarrier(m_ClusterLights);
                computePass.BufferUAVBarrier(m_LightCull);

                // Snapshot for the capacity warning, read back FRAMES_IN_FLIGHT frames from now.
                cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::CopySource);
                computePass.CopyBufferToBuffer(m_LightCull, m_CounterReadback[ctx.frameIndex], 0, 0, kResetWords * sizeof(uint32));
                cmd.MemoryBarrier(agfx::ResourceState::CopySource, agfx::ResourceState::UnorderedAccess);
            }

            // The grid is read as a plain buffer by GBuffer Resolve's heatmap and by every scheme's
            // shading kernel.
            cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::NonPixelShaderResource);
        });
}
