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
    constexpr const char* kPopulateOpaqueIndirectBundleShaderPath = "Content/Shaders/PopulateOpaqueIndirectBundle.hlsl";

    constexpr uint32 kPopulateThreadGroupSize = 64;
    // Translucent/Deferred bundles are unused until the forward/deferred passes exist -- a small
    // nominal capacity is enough to keep them real, usable objects for now.
    constexpr uint32 kNominalUnusedBundleCapacity = 1024;

    struct FrameConstants
    {
        glm::mat4 viewProj;
        glm::vec3 cameraPosition;
        float pad;
    };

    // Mirrors PopulatePushConstants in Content/Shaders/PopulateOpaqueIndirectBundle.hlsl.
    struct PopulatePushConstants
    {
        uint32 rInstanceBuffer;
        uint32 uInstanceCount;
        uint64 uBundleHandle;
        uint32 rDrawIndirection;
    };
}

// Mirrors ScenePushConstants in Content/Shaders/Common/SceneMesh.hlsli. Instances are submitted
// through an indirect bundle rather than one draw call per instance, so there is no per-draw
// uInstanceIndex field -- the mesh shader recovers it from AGFX_DRAW_ID() (with the Vulkan
// indirection-buffer workaround, see SceneMesh.hlsli's SceneResolveInstanceIndex).
struct ScenePushConstants
{
    uint32 rFrameConstants;
    uint32 rInstanceBuffer;
    uint32 rMaterialBuffer;
    uint32 rSchemeParams;
    uint32 rDrawIndirection;
    uint32 rSampler;
    uint32 rFallbackTexture;
};

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
                     .SetSupportsIndirect(true) // required to replay this pipeline from an indirect bundle on Metal
                     .AddColorAttachment(colorFormat);
    ShaderServer::RegisterRenderPipeline(debugPipelineInfo, kDebugMeshletIDShaderPath);

    agfx::ComputePipelineCreateInfo populatePipelineInfo;
    populatePipelineInfo.SetName("Populate Opaque Indirect Bundle Pipeline");
    ShaderServer::RegisterComputePipeline(populatePipelineInfo, kPopulateOpaqueIndirectBundleShaderPath);

    for (uint32 i = 0; i < framesInFlight; ++i)
    {
        agfx::BufferCreateInfo bufferInfo;
        bufferInfo.SetSize(sizeof(FrameConstants)).SetStride(sizeof(FrameConstants)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        m_CameraBuffers[i] = m_Device->CreateBuffer(bufferInfo);
        m_CameraBuffers[i].SetName("Scene Camera Constants");

        agfx::BufferViewCreateInfo viewInfo;
        viewInfo.SetBuffer(m_CameraBuffers[i].Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(false);
        m_CameraBufferViews[i] = m_Device->CreateBufferView(viewInfo);

        m_TranslucentBundles[i] = m_Device->CreateIndirectBundle(agfx::IndirectBundleCreateInfo()
            .SetType(agfx::IndirectBundleType::DrawMesh)
            .SetMaxCommandCount(kNominalUnusedBundleCapacity)
            .SetMaxCountCount(1));
        m_DeferredBundles[i] = m_Device->CreateIndirectBundle(agfx::IndirectBundleCreateInfo()
            .SetType(agfx::IndirectBundleType::Dispatch)
            .SetMaxCommandCount(kNominalUnusedBundleCapacity)
            .SetMaxCountCount(1));
    }

    agfx::BufferCreateInfo zeroBufferInfo;
    zeroBufferInfo.SetSize(sizeof(uint32)).SetStride(sizeof(uint32)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
    m_ZeroBuffer = m_Device->CreateBuffer(zeroBufferInfo);
    m_ZeroBuffer.SetName("Indirect Bundle Zero Buffer");
    {
        agfx::MappedBuffer mapped(m_ZeroBuffer);
        std::memset(mapped.Get(), 0, sizeof(uint32));
    }
}

void SceneRenderer::EnsureOpaqueCapacity(uint32 frameIndex, uint32 requiredCount)
{
    if (requiredCount <= m_OpaqueCapacities[frameIndex])
        return;

    // Overshoot so a steadily growing scene does not reallocate the bundle every single frame,
    // mirroring StreamBuffer::Reserve.
    uint32 newCapacity = requiredCount + requiredCount / 2 + 256;

    m_OpaqueBundles[frameIndex] = m_Device->CreateIndirectBundle(agfx::IndirectBundleCreateInfo()
        .SetType(agfx::IndirectBundleType::DrawMesh)
        .SetMaxCommandCount(newCapacity)
        .SetMaxCountCount(1));

    agfx::BufferCreateInfo indirectionInfo;
    indirectionInfo.SetSize((uint64)newCapacity * sizeof(uint32))
                   .SetStride(sizeof(uint32))
                   .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
                   .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_OpaqueDrawIndirection[frameIndex] = m_Device->CreateBuffer(indirectionInfo);
    m_OpaqueDrawIndirection[frameIndex].SetName("Opaque Indirect Bundle Draw Indirection");

    agfx::BufferViewCreateInfo indirectionViewInfo;
    indirectionViewInfo.SetBuffer(m_OpaqueDrawIndirection[frameIndex].Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_OpaqueDrawIndirectionViews[frameIndex] = m_Device->CreateBufferView(indirectionViewInfo);

    m_OpaqueCapacities[frameIndex] = newCapacity;
    m_Device->MakeResourcesResident();
}

ScenePushConstants SceneRenderer::BuildPushConstants(GPUScene& gpuScene, uint32 frameIndex) const
{
    ScenePushConstants pc{};
    pc.rFrameConstants = (uint32)m_CameraBufferViews[frameIndex].GetHandle();
    pc.rInstanceBuffer = (uint32)gpuScene.GetInstanceBufferView(frameIndex).GetHandle();
    pc.rMaterialBuffer = (uint32)gpuScene.GetMaterialBufferView(frameIndex).GetHandle();
    pc.rSchemeParams = 0u;
    pc.rDrawIndirection = (uint32)m_OpaqueDrawIndirectionViews[frameIndex].GetHandle();
    pc.rSampler = (uint32)m_Sampler.GetHandle();
    pc.rFallbackTexture = gpuScene.GetFallbackTextureHandle();
    return pc;
}

agfx::IndirectBundleExecuteInfo SceneRenderer::BuildExecuteInfo(const ScenePushConstants& pc, uint32 commandCount) const
{
    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(kDebugMeshletIDShaderPath, {});

    agfx::IndirectBundleExecuteInfo info;
    info.SetCountIndex(0)
        .SetCommandRange(0, commandCount)
        .SetPushConstants(pc)
        .SetRenderPipeline(pipeline ? pipeline->Get() : nullptr);
    return info;
}

void SceneRenderer::PrepareIndirectBundles(agfx::CommandBuffer& cmd, GPUScene& gpuScene, uint32 frameIndex)
{
    uint32 count = gpuScene.GetInstanceCount();
    if (count == 0)
        return;

    agfx::ComputePipeline* populatePipeline = ShaderServer::GetComputePipeline(kPopulateOpaqueIndirectBundleShaderPath);
    if (!populatePipeline)
        return;

    EnsureOpaqueCapacity(frameIndex, count);

    agfx::IndirectBundle& bundle = m_OpaqueBundles[frameIndex];
    ScenePushConstants pc = BuildPushConstants(gpuScene, frameIndex);
    agfx::IndirectBundleExecuteInfo executeInfo = BuildExecuteInfo(pc, count);
    if (!executeInfo.renderPipeline)
        return;

    // WAR: the previous use of this frame slot's bundle may still be replaying on the GPU when this
    // frame's populate work starts touching it. Per-frame-slot bundles plus the fence wait at the top
    // of Renderer::Render already rule out an actual hazard here; this is a pure state transition.
    cmd.MemoryBarrier(agfx::ResourceState::IndirectArgument, agfx::ResourceState::UnorderedAccess);

    {
        agfx::ComputePass pass = cmd.BeginComputePass("Populate Opaque Indirect Bundle");

        // Only the count slot needs resetting every frame -- stale commands beyond the live count
        // are never executed (SKILL.md gotcha 7).
        agfxComputePassCopyBufferToBuffer(pass, m_ZeroBuffer, bundle.CountBuffer(), 0, 0, sizeof(uint32));
        cmd.MemoryBarrier(agfx::ResourceState::CopyDest, agfx::ResourceState::UnorderedAccess);

        PopulatePushConstants populatePC{};
        populatePC.rInstanceBuffer = pc.rInstanceBuffer;
        populatePC.uInstanceCount = count;
        populatePC.uBundleHandle = bundle.GetHandle();
        populatePC.rDrawIndirection = pc.rDrawIndirection;

        pass.SetPipeline(*populatePipeline);
        pass.PushConstants(populatePC);
        pass.Dispatch((count + kPopulateThreadGroupSize - 1) / kPopulateThreadGroupSize, 1, 1);

        // The prepare step below reads what the populate dispatch just wrote.
        pass.BufferUAVBarrier(bundle.CommandsBuffer());
        pass.BufferUAVBarrier(bundle.CountBuffer());

        // No-op on D3D12/Vulkan; builds the Metal ICB. Push constants/pipeline must match the
        // execute call exactly -- Metal bakes them into the ICB at this point (SKILL.md gotcha 2).
        pass.PrepareIndirectBundle(bundle, executeInfo);
    }

    cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::IndirectArgument);
}

void SceneRenderer::Render(agfx::RenderPass& renderPass, GPUScene& gpuScene,
                           const Camera& camera, uint32 width, uint32 height, uint32 frameIndex)
{
    uint32 count = gpuScene.GetInstanceCount();
    if (count == 0)
        return;

    {
        FrameConstants constants;
        constants.viewProj = camera.GetViewProjection(height != 0 ? (float)width / (float)height : 1.0f);
        constants.cameraPosition = camera.GetPosition();
        agfx::MappedBuffer mapped(m_CameraBuffers[frameIndex]);
        std::memcpy(mapped.Get(), &constants, sizeof(constants));
    }

    ScenePushConstants pc = BuildPushConstants(gpuScene, frameIndex);
    agfx::IndirectBundleExecuteInfo executeInfo = BuildExecuteInfo(pc, count);
    if (!executeInfo.renderPipeline)
        return;

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);

    // ExecuteIndirectBundle sets both the pipeline and push constants itself, from executeInfo, on
    // every backend -- no separate SetPipeline/PushConstants call here.
    renderPass.ExecuteIndirectBundle(m_OpaqueBundles[frameIndex], executeInfo);
}
