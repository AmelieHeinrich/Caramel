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
    constexpr const char* kDebugMeshletIDShaderPath = "Content/Shaders/DebugMeshletID.hlsl";

    struct FrameConstants
    {
        glm::mat4 viewProj;
        glm::vec3 cameraPosition;
        float pad;
    };

    // Mirrors ScenePushConstants in Content/Shaders/Common/SceneMesh.hlsli. The material/scheme
    // fields are unused by the debug pipeline but kept so the struct still matches the shared
    // shader header used by the (currently unwired) material schemes.
    struct ScenePushConstants
    {
        uint32 rFrameConstants;
        uint32 rInstanceBuffer;
        uint32 rMaterialBuffer;
        uint32 rSchemeParams;
        uint32 uInstanceIndex;
        uint32 rSampler;
        uint32 rFallbackTexture;
    };
}

SceneRenderer::SceneRenderer(agfx::Device& device, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat, uint32 framesInFlight)
    : m_Device(&device)
{
    agfx::SamplerCreateInfo samplerInfo;
    samplerInfo.SetFilter(agfx::SamplerFilter::Linear)
               .SetAddressMode(agfx::AddressMode::Repeat)
               .SetComparisonFunction(agfx::ComparisonFunction::Always)
               .SetLodRange(0.0f, 16.0f);
    m_Sampler = m_Device->CreateSampler(samplerInfo);

    agfx::RenderPipelineCreateInfo debugPipelineInfo;
    debugPipelineInfo.SetName("Scene Debug Meshlet ID Pipeline")
                     .SetCullMode(agfx::CullMode::None)
                     .SetFrontFace(agfx::FrontFace::CounterClockwise)
                     .SetTopology(agfx::Topology::Triangles)
                     .SetDepthState(true, true, agfx::ComparisonFunction::Less)
                     .SetDepthFormat(depthFormat)
                     .AddColorAttachment(colorFormat);
    ShaderServer::RegisterRenderPipeline(debugPipelineInfo, kDebugMeshletIDShaderPath);

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

void SceneRenderer::Render(agfx::RenderPass& renderPass, GPUScene& gpuScene,
                           const Camera& camera, uint32 width, uint32 height, uint32 frameIndex)
{
    const TArray<GPUDraw>& draws = gpuScene.GetDraws();
    if (draws.IsEmpty())
        return;

    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(kDebugMeshletIDShaderPath, {});
    if (!pipeline)
        return;

    {
        FrameConstants constants;
        constants.viewProj = camera.GetViewProjection(height != 0 ? (float)width / (float)height : 1.0f);
        constants.cameraPosition = camera.GetPosition();
        agfx::MappedBuffer mapped(m_CameraBuffers[frameIndex]);
        std::memcpy(mapped.Get(), &constants, sizeof(constants));
    }

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);
    renderPass.SetPipeline(*pipeline);

    ScenePushConstants pc{};
    pc.rFrameConstants = (uint32)m_CameraBufferViews[frameIndex].GetHandle();
    pc.rInstanceBuffer = (uint32)gpuScene.GetInstanceBufferView(frameIndex).GetHandle();
    pc.rMaterialBuffer = (uint32)gpuScene.GetMaterialBufferView(frameIndex).GetHandle();
    pc.rSchemeParams = 0u;
    pc.rSampler = (uint32)m_Sampler.GetHandle();
    pc.rFallbackTexture = gpuScene.GetFallbackTextureHandle();

    for (const GPUDraw& draw : draws)
    {
        pc.uInstanceIndex = draw.instanceIndex;
        renderPass.PushConstants(pc);
        renderPass.DrawMesh(draw.meshletCount, 1, 1);
    }
}
