/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:30:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "SceneRenderer.hpp"

#include <Caramel/Renderer/Shader/ShaderServer.hpp>

#include <cstring>

namespace
{
    struct FrameConstants
    {
        glm::mat4 viewProj;
    };

    // Mirrors ScenePushConstants in Content/Shaders/Common/SceneMesh.hlsli.
    struct ScenePushConstants
    {
        uint32 rFrameConstants;
        uint32 rInstanceBuffer;
        uint32 rMaterialBuffer;
        uint32 rSchemeParams;
        uint32 uInstanceIndex;
        uint32 rSampler;
    };
}

SceneRenderer::SceneRenderer(agfx::Device& device, uint32 framesInFlight)
    : m_Device(&device)
{
    agfx::SamplerCreateInfo samplerInfo;
    samplerInfo.SetFilter(agfx::SamplerFilter::Linear)
               .SetAddressMode(agfx::AddressMode::Repeat)
               .SetComparisonFunction(agfx::ComparisonFunction::Always)
               .SetLodRange(0.0f, 16.0f);
    m_Sampler = m_Device->CreateSampler(samplerInfo);

    for (uint32 i = 0; i < framesInFlight; ++i)
    {
        agfx::BufferCreateInfo bufferInfo;
        bufferInfo.SetSize(sizeof(FrameConstants)).SetStride(sizeof(FrameConstants)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        m_CameraBuffers[i] = m_Device->CreateBuffer(bufferInfo);
        m_CameraBuffers[i].SetName("Scene Camera Constants");

        agfx::BufferViewCreateInfo viewInfo;
        viewInfo.SetBuffer(m_CameraBuffers[i].Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(false);
        m_CameraBufferViews[i] = m_Device->CreateBufferView(viewInfo);
    }
}

void SceneRenderer::Render(agfx::RenderPass& renderPass, GPUScene& gpuScene, const SchemeRegistry& schemes,
                           const Camera& camera, uint32 width, uint32 height, uint32 frameIndex)
{
    const TArray<SchemeBucket>& buckets = gpuScene.GetBuckets();
    if (buckets.IsEmpty())
        return;

    {
        FrameConstants constants;
        constants.viewProj = camera.GetViewProjection(height != 0 ? (float)width / (float)height : 1.0f);
        agfx::MappedBuffer mapped(m_CameraBuffers[frameIndex]);
        std::memcpy(mapped.Get(), &constants, sizeof(constants));
    }

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);

    ScenePushConstants pc{};
    pc.rFrameConstants = (uint32)m_CameraBufferViews[frameIndex].GetHandle();
    pc.rInstanceBuffer = (uint32)gpuScene.GetInstanceBufferView(frameIndex).GetHandle();
    pc.rMaterialBuffer = (uint32)gpuScene.GetMaterialBufferView(frameIndex).GetHandle();
    pc.rSampler = (uint32)m_Sampler.GetHandle();

    const TArray<MaterialBatch>& batches = gpuScene.GetBatches();
    const TArray<GPUDraw>& draws = gpuScene.GetDraws();

    for (const SchemeBucket& bucket : buckets)
    {
        const MaterialScheme& scheme = schemes.Get(bucket.schemeId);

        agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(scheme.shaderPath, {});
        if (!pipeline)
            continue;

        renderPass.SetPipeline(*pipeline);

        pc.rSchemeParams = scheme.paramStride > 0
            ? (uint32)gpuScene.GetSchemeParamBufferView(bucket.schemeId, frameIndex).GetHandle()
            : 0u;

        for (uint32 b = bucket.firstBatch; b < bucket.firstBatch + bucket.batchCount; ++b)
        {
            const MaterialBatch& batch = batches[b];

            for (uint32 d = batch.firstDraw; d < batch.firstDraw + batch.drawCount; ++d)
            {
                pc.uInstanceIndex = draws[d].instanceIndex;
                renderPass.PushConstants(pc);
                renderPass.DrawMesh(draws[d].meshletCount, 1, 1);
            }
        }
    }
}
