/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:30:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "SponzaRenderer.hpp"

#include <Caramel/Renderer/Shader/ShaderServer.hpp>

#include <cstring>

namespace
{
    constexpr const char* kSponzaShaderPath = "Content/Shaders/Sponza.hlsl";

    struct InstanceData
    {
        glm::mat4 model;
    };

    struct FrameConstants
    {
        glm::mat4 viewProj;
    };

    // Mirrors Content/Shaders/Sponza.hlsl's SponzaPushConstants field-for-field; HLSL's
    // ResourceHandle is a plain uint, so this side just uses uint32 (same convention as
    // ImGuiRenderer::ImGuiPushConstants).
    struct SponzaPushConstants
    {
        uint32 rFrameConstants;
        uint32 rInstanceBuffer;
        uint32 uInstanceIndex;
        uint32 rVertexBuffer;
        uint32 rMeshletBuffer;
        uint32 rMeshletVertexBuffer;
        uint32 rMeshletTriangleBuffer;
        uint32 rBaseColorTexture;
        uint32 rSampler;
    };
}

SponzaRenderer::SponzaRenderer(agfx::Device& device, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat, uint32 framesInFlight)
    : m_Device(&device)
{
    agfx::RenderPipelineCreateInfo pipelineInfo;
    pipelineInfo.SetName("Sponza Mesh Shader Pipeline")
                .SetCullMode(agfx::CullMode::None)
                .SetFrontFace(agfx::FrontFace::CounterClockwise)
                .SetTopology(agfx::Topology::Triangles)
                .SetDepthState(true, true, agfx::ComparisonFunction::Less)
                .SetDepthFormat(depthFormat)
                .AddColorAttachment(colorFormat);
    ShaderServer::RegisterRenderPipeline(pipelineInfo, kSponzaShaderPath);

    agfx::SamplerCreateInfo samplerInfo;
    samplerInfo.SetFilter(agfx::SamplerFilter::Linear)
               .SetAddressMode(agfx::AddressMode::Repeat)
               .SetComparisonFunction(agfx::ComparisonFunction::Always)
               .SetLodRange(0.0f, 16.0f);
    m_Sampler = m_Device->CreateSampler(samplerInfo);

    // 1x1 white fallback so every mesh always has a valid bindless texture handle even before its
    // material's base color texture has streamed its first mip in.
    {
        agfx::TextureCreateInfo textureInfo;
        textureInfo.SetType(agfx::TextureType::Texture2D)
                   .SetFormat(agfx::TextureFormat::RGBA8Unorm)
                   .SetUsage(agfx::TextureUsage::Sampled)
                   .SetSize(1, 1, 1)
                   .SetMipLevels(1);
        m_FallbackTexture = m_Device->CreateTexture(textureInfo);
        m_FallbackTexture.SetName("Sponza Fallback Texture");

        agfx::TextureViewCreateInfo viewInfo;
        viewInfo.SetTexture(m_FallbackTexture.Get())
                .SetFormat(agfx::TextureFormat::RGBA8Unorm)
                .SetType(agfx::TextureType::Texture2D)
                .SetMipRange(0, 1)
                .SetArrayRange(0, 1)
                .SetWriteable(false);
        m_FallbackTextureView = m_Device->CreateTextureView(viewInfo);
    }

    for (uint32 i = 0; i < framesInFlight; ++i)
    {
        // Read in the shader as a 1-element AGFXStructuredBuffer<FrameConstants> (same bindless SRV
        // path as every other buffer here) rather than a real HLSL cbuffer -- there's no existing
        // bindless-constant-buffer wrapper in AGFX.hlsli to reuse, and one isn't worth adding for a
        // single mat4.
        agfx::BufferCreateInfo bufferInfo;
        bufferInfo.SetSize(sizeof(FrameConstants)).SetStride(sizeof(FrameConstants)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        m_CameraBuffers[i] = m_Device->CreateBuffer(bufferInfo);
        m_CameraBuffers[i].SetName("Sponza Camera Constants");

        agfx::BufferViewCreateInfo viewInfo;
        viewInfo.SetBuffer(m_CameraBuffers[i].Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(false);
        m_CameraBufferViews[i] = m_Device->CreateBufferView(viewInfo);
    }
}

void SponzaRenderer::EnsureInstanceBuffer(StreamingManager& streamingManager)
{
    const TArray<TShared<StreamingModel>>& models = streamingManager.GetModels();
    if (models.Size() <= m_InstanceCount && models.Size() <= m_InstanceCapacity)
        return;

    // Sponza is static: instance data (one model matrix per mesh) never changes once written, so
    // the buffer is only (re)built when new meshes show up -- not every frame.
    uint32 newCapacity = (uint32)models.Size();
    agfx::BufferCreateInfo bufferInfo;
    bufferInfo.SetSize(sizeof(InstanceData) * newCapacity).SetStride(sizeof(InstanceData)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
    m_InstanceBuffer = m_Device->CreateBuffer(bufferInfo);
    m_InstanceBuffer.SetName("Sponza Instance Buffer");

    agfx::BufferViewCreateInfo viewInfo;
    viewInfo.SetBuffer(m_InstanceBuffer.Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(false);
    m_InstanceBufferView = m_Device->CreateBufferView(viewInfo);

    {
        agfx::MappedBuffer mapped(m_InstanceBuffer);
        InstanceData* dst = mapped.As<InstanceData>();
        for (uint32 i = 0; i < newCapacity; ++i)
            dst[i].model = models[i]->GetWorldTransform();
    }

    m_InstanceCapacity = newCapacity;
    m_InstanceCount = newCapacity;
}

void SponzaRenderer::Render(agfx::RenderPass& renderPass, StreamingManager& streamingManager, const Camera& camera, uint32 width, uint32 height, uint32 frameIndex)
{
    const TArray<TShared<StreamingModel>>& models = streamingManager.GetModels();
    if (models.IsEmpty())
        return;

    EnsureInstanceBuffer(streamingManager);

    {
        FrameConstants constants;
        constants.viewProj = camera.GetViewProjection(height != 0 ? (float)width / (float)height : 1.0f);
        agfx::MappedBuffer mapped(m_CameraBuffers[frameIndex]);
        std::memcpy(mapped.Get(), &constants, sizeof(constants));
    }

    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(kSponzaShaderPath, {});
    if (!pipeline)
        return;

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);
    renderPass.SetPipeline(*pipeline);

    for (uint32 i = 0; i < models.Size(); ++i)
    {
        StreamingModel& model = *models[i];
        uint32 lod = model.SnapshotResidentLOD();
        if (lod == StreamingModel::kNoResidentLOD)
            continue; // Vertex buffer/first LOD hasn't landed yet.

        uint32 meshletCount = model.GetMeshletCount(lod);
        if (meshletCount == 0)
            continue;

        TShared<StreamingTexture> baseColor = streamingManager.GetMaterialTexture(model.GetMesh().materialIndex);
        bool hasResidentTexture = baseColor && baseColor->SnapshotResidentMip() != StreamingTexture::kNoResidentMip;

        SponzaPushConstants pc{};
        pc.rFrameConstants = (uint32)m_CameraBufferViews[frameIndex].GetHandle();
        pc.rInstanceBuffer = (uint32)m_InstanceBufferView.GetHandle();
        pc.uInstanceIndex = i;
        pc.rVertexBuffer = (uint32)model.GetVertexBufferView().GetHandle();
        pc.rMeshletBuffer = (uint32)model.GetMeshletBufferView(lod).GetHandle();
        pc.rMeshletVertexBuffer = (uint32)model.GetMeshletVertexBufferView(lod).GetHandle();
        pc.rMeshletTriangleBuffer = (uint32)model.GetMeshletTriangleBufferView(lod).GetHandle();
        pc.rBaseColorTexture = hasResidentTexture ? (uint32)(intptr_t)baseColor->GetDisplayTexID() : (uint32)m_FallbackTextureView.GetHandle();
        pc.rSampler = (uint32)m_Sampler.GetHandle();

        renderPass.PushConstants(pc);
        renderPass.DrawMesh(meshletCount, 1, 1);
    }
}
