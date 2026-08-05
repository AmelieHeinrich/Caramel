/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:30:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "SceneRenderer.hpp"

#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <Caramel/Renderer/DebugRenderer.hpp>
#include <Caramel/Core/CVar.hpp>

#include <cfloat>
#include <cstring>

namespace
{
    // Off (default): color by a hash of (instance, meshlet) ID. On: color by a hash of
    // (instance, meshlet, triangle) ID instead, for inspecting individual triangles within a
    // meshlet rather than the meshlet as a whole.
    CVar cv_ShowPrimitiveID("scene.show_primitive_id", false, "Show Primitive ID", "Scene Renderer",
        "Color by triangle (primitive) ID instead of the default per-meshlet ID");

    // Off (default): color by a hash of (instance, meshlet[, triangle]) ID. On: color by the
    // selected LOD instead (see DebugMeshletID.hlsl's LODToColor) -- ground truth for whether
    // PopulateOpaqueIndirectBundleCS's distance-based LOD selection is actually changing which
    // meshlet/vertex/triangle buffers get read, independent of triangle density being hard to
    // eyeball at a glance.
    CVar cv_ShowSelectedLOD("scene.show_selected_lod", false, "Show Selected LOD", "Scene Renderer",
        "Color by the GPU-selected LOD (red=0/coarsest .. magenta=4/finest) instead of meshlet ID");

    // Off (default): every camera-driven FrameConstants field tracks the live camera every frame.
    // On: all of them freeze at whatever the camera was the moment this was turned on -- view,
    // projection, their inverses, frustum planes, camera position, near/far -- except
    // viewProjection, which keeps tracking the live camera so the scene still renders from wherever
    // you fly to. That lets frustum, cone, and (eventually) contribution culling all be inspected
    // from outside the frozen viewpoint instead of just the debug-drawn frustum shape.
    CVar cv_FreezeFrustum("scene.freeze_frustum", false, "Freeze Frustum", "Scene Renderer",
        "Stop updating the camera data culling reads (frustum, position, view/projection) and debug-draw the frustum frozen in place");

    // Off (default): no instance-bounds overlay. On: debug-draw every scene instance's world-space
    // AABB, letting frustum-culling bugs (e.g. instances vanishing that should be visible) be
    // inspected visually against the frustum drawn by scene.freeze_frustum.
    CVar cv_ShowInstanceAABBs("scene.show_instance_aabbs", false, "Show Instance AABBs", "Scene Renderer",
        "Debug-draw the world-space AABB of every instance in the scene");

    // Distance (world units) from the camera at which the finest LOD begins stepping down; each
    // further boundary scales by scene.lod_distance_multiplier (geometric falloff). No hysteresis --
    // recomputed fresh every frame in PopulateOpaqueIndirectBundleCS.
    CVar cv_LodBaseDistance("scene.lod_base_distance", 20.0f, 0.0f, 100000.0f, "LOD Base Distance", "Scene Renderer",
        "Distance from the camera at which the finest LOD starts dropping a level");

    CVar cv_LodDistanceMultiplier("scene.lod_distance_multiplier", 2.0f, 1.01f, 10.0f, "LOD Distance Multiplier", "Scene Renderer",
        "Geometric falloff applied to scene.lod_base_distance for each successive LOD boundary");

    constexpr const char* kDebugMeshletIDShaderPath = "Content/Shaders/DebugMeshletID.hlsl";
    constexpr const char* kDebugMeshletIDTwoSidedShaderPath = "Content/Shaders/DebugMeshletIDTwoSided.hlsl";
    constexpr const char* kPopulateOpaqueIndirectBundleShaderPath = "Content/Shaders/PopulateOpaqueIndirectBundle.hlsl";

    constexpr uint32 kPopulateThreadGroupSize = 64;
    // Opaque bundle regions: 0 = single-sided (CullMode::Back), 1 = double-sided (CullMode::None).
    constexpr uint32 kOpaqueRegionCount = 2;
    // Translucent/Deferred bundles are unused until the forward/deferred passes exist -- a small
    // nominal capacity is enough to keep them real, usable objects for now.
    constexpr uint32 kNominalUnusedBundleCapacity = 1024;

    struct FrameConstants
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 viewProjection;
        glm::mat4 invView;
        glm::mat4 invProjection;
        glm::mat4 invViewProjection;
        glm::vec4 frustumPlanes[6];
        glm::vec3 cameraPosition;
        float nearPlane;
        float farPlane;
        glm::vec3 pad;
    };

    // Mirrors PopulatePushConstants in Content/Shaders/PopulateOpaqueIndirectBundle.hlsl.
    struct PopulatePushConstants
    {
        uint32 rInstanceBuffer;
        uint32 uInstanceCount;
        uint64 uBundleHandle;
        uint32 rDrawIndirection;
        uint32 rFrameConstants;
        uint32 rInstanceLodTable;
        uint32 rSelectedLodBuffer;
        float32 fLodBaseDistance;
        float32 fLodDistanceMultiplier;
        uint32 rMaterialBuffer;
        uint32 uRegionCapacity;
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
    uint32 uDebugFlags;
    uint32 rInstanceLodTable;
    uint32 rSelectedLodBuffer;
    uint32 uDrawIndirectionBase;
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
                     .SetCullMode(agfx::CullMode::Back)
                     .SetFrontFace(agfx::FrontFace::CounterClockwise)
                     .SetTopology(agfx::Topology::Triangles)
                     .SetDepthState(true, true, agfx::ComparisonFunction::Less)
                     .SetDepthFormat(depthFormat)
                     .SetSupportsIndirect(true) // required to replay this pipeline from an indirect bundle on Metal
                     .AddColorAttachment(colorFormat);
    ShaderServer::RegisterRenderPipeline(debugPipelineInfo, kDebugMeshletIDShaderPath);

    // Same shader, no backface culling -- replays the double-sided region of the opaque bundle
    // (double-sided materials legitimately show their back faces).
    agfx::RenderPipelineCreateInfo twoSidedPipelineInfo = debugPipelineInfo;
    twoSidedPipelineInfo.SetName("Scene Debug Meshlet ID Two-Sided Pipeline")
                        .SetCullMode(agfx::CullMode::None);
    ShaderServer::RegisterRenderPipeline(twoSidedPipelineInfo, kDebugMeshletIDTwoSidedShaderPath);

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
    zeroBufferInfo.SetSize(kOpaqueRegionCount * sizeof(uint32)).SetStride(sizeof(uint32)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
    m_ZeroBuffer = m_Device->CreateBuffer(zeroBufferInfo);
    m_ZeroBuffer.SetName("Indirect Bundle Zero Buffer");
    {
        agfx::MappedBuffer mapped(m_ZeroBuffer);
        std::memset(mapped.Get(), 0, kOpaqueRegionCount * sizeof(uint32));
    }
}

void SceneRenderer::EnsureOpaqueCapacity(uint32 frameIndex, uint32 requiredCount)
{
    if (requiredCount <= m_OpaqueCapacities[frameIndex])
        return;

    // Overshoot so a steadily growing scene does not reallocate the bundle every single frame,
    // mirroring StreamBuffer::Reserve.
    uint32 newCapacity = requiredCount + requiredCount / 2 + 256;

    // Two regions share the commands buffer (SKILL.md gotcha 4): [0, newCapacity) single-sided
    // draws counted in slot 0, [newCapacity, 2 * newCapacity) double-sided draws counted in slot 1.
    m_OpaqueBundles[frameIndex] = m_Device->CreateIndirectBundle(agfx::IndirectBundleCreateInfo()
        .SetType(agfx::IndirectBundleType::DrawMesh)
        .SetMaxCommandCount(newCapacity * kOpaqueRegionCount)
        .SetMaxCountCount(kOpaqueRegionCount));

    agfx::BufferCreateInfo indirectionInfo;
    indirectionInfo.SetSize((uint64)newCapacity * kOpaqueRegionCount * sizeof(uint32))
                   .SetStride(sizeof(uint32))
                   .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
                   .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_OpaqueDrawIndirection[frameIndex] = m_Device->CreateBuffer(indirectionInfo);
    m_OpaqueDrawIndirection[frameIndex].SetName("Opaque Indirect Bundle Draw Indirection");

    agfx::BufferViewCreateInfo indirectionViewInfo;
    indirectionViewInfo.SetBuffer(m_OpaqueDrawIndirection[frameIndex].Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_OpaqueDrawIndirectionViews[frameIndex] = m_Device->CreateBufferView(indirectionViewInfo);

    agfx::BufferCreateInfo selectedLodInfo;
    selectedLodInfo.SetSize((uint64)newCapacity * sizeof(uint32))
                   .SetStride(sizeof(uint32))
                   .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
                   .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_SelectedLod[frameIndex] = m_Device->CreateBuffer(selectedLodInfo);
    m_SelectedLod[frameIndex].SetName("Opaque Instance Selected LOD");

    agfx::BufferViewCreateInfo selectedLodViewInfo;
    selectedLodViewInfo.SetBuffer(m_SelectedLod[frameIndex].Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(true);
    m_SelectedLodViews[frameIndex] = m_Device->CreateBufferView(selectedLodViewInfo);

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
    pc.uDebugFlags = (*cv_ShowPrimitiveID.AsBoolPtr() ? 1u : 0u) | (*cv_ShowSelectedLOD.AsBoolPtr() ? 2u : 0u);
    pc.rInstanceLodTable = (uint32)gpuScene.GetInstanceLodTableView(frameIndex).GetHandle();
    pc.rSelectedLodBuffer = (uint32)m_SelectedLodViews[frameIndex].GetHandle();
    return pc;
}

// Builds the execute info for one bundle region. Also used at prepare time -- Metal bakes the
// pipeline and push constants into the ICB then (SKILL.md gotcha 2), so prepare and execute must
// receive identical infos, which sharing this helper guarantees.
agfx::IndirectBundleExecuteInfo SceneRenderer::BuildRegionExecuteInfo(const ScenePushConstants& pc, uint32 region, uint32 frameIndex) const
{
    uint32 capacity = m_OpaqueCapacities[frameIndex];
    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(
        region == 0 ? kDebugMeshletIDShaderPath : kDebugMeshletIDTwoSidedShaderPath, {});

    ScenePushConstants regionPC = pc;
    regionPC.uDrawIndirectionBase = region * capacity;

    agfx::IndirectBundleExecuteInfo info;
    info.SetCountIndex(region)
        .SetCommandRange(region * capacity, capacity)
        .SetPushConstants(regionPC)
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
    agfx::IndirectBundleExecuteInfo regionInfos[kOpaqueRegionCount];
    for (uint32 region = 0; region < kOpaqueRegionCount; ++region)
    {
        regionInfos[region] = BuildRegionExecuteInfo(pc, region, frameIndex);
        if (!regionInfos[region].renderPipeline)
            return;
    }

    // WAR: the previous use of this frame slot's bundle may still be replaying on the GPU when this
    // frame's populate work starts touching it. Per-frame-slot bundles plus the fence wait at the top
    // of Renderer::Render already rule out an actual hazard here; this is a pure state transition.
    cmd.MemoryBarrier(agfx::ResourceState::IndirectArgument, agfx::ResourceState::UnorderedAccess);

    {
        agfx::ComputePass pass = cmd.BeginComputePass("Populate Opaque Indirect Bundle");

        // Only the count slots need resetting every frame -- stale commands beyond the live counts
        // are never executed (SKILL.md gotcha 7).
        agfxComputePassCopyBufferToBuffer(pass, m_ZeroBuffer, bundle.CountBuffer(), 0, 0, kOpaqueRegionCount * sizeof(uint32));
        cmd.MemoryBarrier(agfx::ResourceState::CopyDest, agfx::ResourceState::UnorderedAccess);

        PopulatePushConstants populatePC{};
        populatePC.rInstanceBuffer = pc.rInstanceBuffer;
        populatePC.uInstanceCount = count;
        populatePC.uBundleHandle = bundle.GetHandle();
        populatePC.rDrawIndirection = pc.rDrawIndirection;
        populatePC.rFrameConstants = pc.rFrameConstants;
        populatePC.rInstanceLodTable = pc.rInstanceLodTable;
        populatePC.rSelectedLodBuffer = pc.rSelectedLodBuffer;
        populatePC.fLodBaseDistance = *cv_LodBaseDistance.AsFloatPtr();
        populatePC.fLodDistanceMultiplier = *cv_LodDistanceMultiplier.AsFloatPtr();
        populatePC.rMaterialBuffer = pc.rMaterialBuffer;
        populatePC.uRegionCapacity = m_OpaqueCapacities[frameIndex];

        pass.SetPipeline(*populatePipeline);
        pass.PushConstants(populatePC);
        pass.Dispatch((count + kPopulateThreadGroupSize - 1) / kPopulateThreadGroupSize, 1, 1);

        // The prepare step below reads what the populate dispatch just wrote.
        pass.BufferUAVBarrier(bundle.CommandsBuffer());
        pass.BufferUAVBarrier(bundle.CountBuffer());

        // No-op on D3D12/Vulkan; builds the Metal ICB. Push constants/pipeline must match the
        // execute call exactly -- Metal bakes them into the ICB at this point (SKILL.md gotcha 2).
        for (uint32 region = 0; region < kOpaqueRegionCount; ++region)
            pass.PrepareIndirectBundle(bundle, regionInfos[region]);
    }

    cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::IndirectArgument);

    // Populate just wrote m_SelectedLod via a UAV; SceneAS reads it as a plain structured-buffer SRV
    // in the render pass's task-shader stage, which is a *different* consumer sync scope than
    // IndirectArgument above -- that needs its own MemoryBarrier call, not reuse of the bundle's.
    cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::NonPixelShaderResource);
}

void SceneRenderer::Render(agfx::RenderPass& renderPass, GPUScene& gpuScene,
                           const Camera& camera, uint32 width, uint32 height, uint32 frameIndex)
{
    uint32 count = gpuScene.GetInstanceCount();
    if (count == 0)
        return;

    {
        float aspectRatio = height != 0 ? (float)width / (float)height : 1.0f;

        FrameConstants constants;
        // Always live: this is the only field SceneMS actually reads to place geometry on screen,
        // so it has to keep tracking the real camera even while everything else below is frozen --
        // otherwise flying away from a frozen frustum to inspect culling would freeze the view too.
        constants.viewProjection = camera.GetViewProjection(aspectRatio);

        if (!*cv_FreezeFrustum.AsBoolPtr())
        {
            m_FrozenView = camera.GetView();
            m_FrozenProjection = camera.GetProjection(aspectRatio);
            m_FrozenInvView = glm::inverse(m_FrozenView);
            m_FrozenInvProjection = glm::inverse(m_FrozenProjection);
            m_FrozenInvViewProjection = glm::inverse(constants.viewProjection);
            camera.GetFrustumPlanes(aspectRatio, m_FrozenFrustumPlanes);
            m_FrozenFrustumViewProjection = constants.viewProjection;
            m_FrozenCameraPosition = camera.GetPosition();
            m_FrozenNearPlane = camera.GetNearPlane();
            m_FrozenFarPlane = camera.GetFarPlane();
        }

        constants.view = m_FrozenView;
        constants.projection = m_FrozenProjection;
        constants.invView = m_FrozenInvView;
        constants.invProjection = m_FrozenInvProjection;
        constants.invViewProjection = m_FrozenInvViewProjection;
        std::memcpy(constants.frustumPlanes, m_FrozenFrustumPlanes, sizeof(constants.frustumPlanes));
        constants.cameraPosition = m_FrozenCameraPosition;
        constants.nearPlane = m_FrozenNearPlane;
        constants.farPlane = m_FrozenFarPlane;
        agfx::MappedBuffer mapped(m_CameraBuffers[frameIndex]);
        std::memcpy(mapped.Get(), &constants, sizeof(constants));
    }

    if (*cv_FreezeFrustum.AsBoolPtr())
    {
        DebugStyle style;
        style.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
        style.depthTest = false;
        DebugRenderer::Get().Frustum(m_FrozenFrustumViewProjection, style);
    }

    if (*cv_ShowInstanceAABBs.AsBoolPtr())
    {
        DebugStyle style;
        style.color = glm::vec4(0.0f, 1.0f, 0.4f, 1.0f);
        style.depthTest = false;

        for (const GPUInstance& instance : gpuScene.GetInstances())
        {
            glm::vec3 worldMin(FLT_MAX);
            glm::vec3 worldMax(-FLT_MAX);
            for (uint32 corner = 0; corner < 8; ++corner)
            {
                glm::vec3 localCorner(
                    (corner & 1) ? instance.boundsMax.x : instance.boundsMin.x,
                    (corner & 2) ? instance.boundsMax.y : instance.boundsMin.y,
                    (corner & 4) ? instance.boundsMax.z : instance.boundsMin.z);
                glm::vec3 worldCorner = glm::vec3(instance.transform * glm::vec4(localCorner, 1.0f));
                worldMin = glm::min(worldMin, worldCorner);
                worldMax = glm::max(worldMax, worldCorner);
            }
            DebugRenderer::Get().Box(worldMin, worldMax, style);
        }
    }

    ScenePushConstants pc = BuildPushConstants(gpuScene, frameIndex);
    agfx::IndirectBundleExecuteInfo regionInfos[kOpaqueRegionCount];
    for (uint32 region = 0; region < kOpaqueRegionCount; ++region)
    {
        regionInfos[region] = BuildRegionExecuteInfo(pc, region, frameIndex);
        if (!regionInfos[region].renderPipeline)
            return;
    }

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);

    // ExecuteIndirectBundle sets both the pipeline and push constants itself, from executeInfo, on
    // every backend -- no separate SetPipeline/PushConstants call here.
    for (uint32 region = 0; region < kOpaqueRegionCount; ++region)
        renderPass.ExecuteIndirectBundle(m_OpaqueBundles[frameIndex], regionInfos[region]);
}
