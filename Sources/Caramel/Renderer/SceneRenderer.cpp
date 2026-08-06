/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:30:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "SceneRenderer.hpp"

#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <Caramel/Renderer/DebugRenderer.hpp>
#include <Caramel/Core/CVar.hpp>
#include <Caramel/Core/Logger.hpp>

#include <cstring>

#if defined(CARAMEL_WINDOWS)
    #undef MemoryBarrier
#endif

namespace
{
    // Off: nothing is culled at all -- every instance is submitted and every meshlet reaches the mesh
    // shader. The ground truth to diff against when something disappears that should not have.
    CVar cv_EnableCulling("scene.enable_culling", true, "Enable Culling", "Scene Renderer",
        "Instance frustum/occlusion culling and meshlet frustum/cone/contribution/occlusion culling");

    // Off (default): every camera-driven FrameConstants field tracks the live camera every frame.
    // On: all of them freeze at whatever the camera was the moment this was turned on -- view,
    // projection, their inverses, frustum planes, camera position, near/far -- except
    // viewProjection, which keeps tracking the live camera so the scene still renders from wherever
    // you fly to. That lets frustum and cone culling be inspected from outside the frozen viewpoint
    // instead of just the debug-drawn frustum shape. Occlusion culling is unaffected by design: it
    // projects with hzbViewProjection, the camera the pyramid was actually rasterized from.
    CVar cv_FreezeFrustum("scene.freeze_frustum", false, "Freeze Frustum", "Scene Renderer",
        "Stop updating the camera data culling reads (frustum, position, view/projection) and debug-draw the frustum frozen in place");

    // Which gbuffer channel or ID hash GBufferResolve.hlsl writes into the scene lighting buffer.
    // Anything other than 0 suppresses material classification and shading entirely -- both write the
    // same target, so leaving them on would just overwrite the view being inspected.
    CVar cv_GBufferDebug("scene.gbuffer_debug", 0, 0, 9, "GBuffer Debug", "Scene Renderer",
        "0 = none (deferred shading), 1 = albedo, 2 = normal, 3 = metallic/roughness, 4 = emissive, 5 = motion, 6 = meshlet ID, 7 = triangle ID, 8 = instance ID, 9 = LOD");

    // Off: the depth pyramid stops being rebuilt and freezes at whatever it last held, together with
    // the viewProjection occlusion tests project bounds with (hzbViewProjection). Every occlusion
    // decision is then made from the frozen viewpoint, so flying away reveals over-culling as holes
    // anchored in world space behind the old occluders.
    CVar cv_BuildHZB("scene.build_hzb", true, "Build HZB", "Scene Renderer",
        "Rebuild the depth pyramid every frame. Off freezes it in place for debugging occlusion culling");

    constexpr const char* kVisBufferShaderPath = "Content/Shaders/VisBuffer.hlsl";
    constexpr const char* kVisBufferTwoSidedShaderPath = "Content/Shaders/VisBufferTwoSided.hlsl";
    constexpr const char* kGBufferResolveShaderPath = "Content/Shaders/GBufferResolve.hlsl";
    constexpr const char* kPopulateOpaqueIndirectBundleShaderPath = "Content/Shaders/PopulateOpaqueIndirectBundle.hlsl";
    constexpr const char* kBuildHZBShaderPath = "Content/Shaders/BuildHZB.hlsl";
    constexpr const char* kClassifyCountShaderPath = "Content/Shaders/MaterialClassifyCount.hlsl";
    constexpr const char* kClassifyArgsShaderPath = "Content/Shaders/MaterialClassifyArgs.hlsl";
    constexpr const char* kClassifyScatterShaderPath = "Content/Shaders/MaterialClassifyScatter.hlsl";
    constexpr const char* kCompositeShaderPath = "Content/Shaders/Composite.hlsl";

    // All three mirror Content/Shaders/Common/DeferredShading.hlsli and must not drift from it.
    constexpr uint32 kClassifyTileSize = 8;
    constexpr uint32 kClassifySlotsPerScheme = 3;   // counts, offsets, cursors
    constexpr uint32 kClassifySlotCount = SchemeRegistry::kMaxSchemes * kClassifySlotsPerScheme;

    // DeferredPackPixel packs a coordinate as (y << 16) | x.
    constexpr uint32 kMaxClassifiedViewportExtent = 0xFFFFu;

    constexpr uint32 kPopulateThreadGroupSize = 64;
    // The downsampler's group owns a 64x64 tile of HZB mip 0. Mirrors kHZBTileSize in BuildHZB.hlsl.
    constexpr uint32 kHZBTileSize = 64;

    // Opaque bundle regions: region = pass * 4 + alphaTested * 2 + doubleSided. Within a pass the
    // replay order is opaque Back/None then alpha-tested Back/None, so alpha-tested draws always
    // execute after every fully opaque draw of that pass.
    constexpr uint32 kOpaqueRegionsPerPass = 4;
    constexpr uint32 kOpaqueRegionCount = 8;
    constexpr uint32 kPassEarly = 0;
    constexpr uint32 kPassLate = 1;

    constexpr uint32 kCullFlagEnabled = 1u;
    constexpr uint32 kCullFlagHZBValid = 2u;
    // Bit 2 means something different per consumer: "visibility flags are trustworthy" in the populate
    // shader's block, "this region belongs to the late pass" in SceneAS's. Bits 0/1 are shared via
    // the copy in RecordCullPass, so bit 2 must only ever be set at each consumer's build site --
    // never in BuildPushConstants, which feeds both.
    constexpr uint32 kCullFlagVisibilityValid = 4u;
    constexpr uint32 kSceneCullFlagLatePass = 4u;

    // Translucent/Deferred bundles are unused until the forward/deferred passes exist -- a small
    // nominal capacity is enough to keep them real, usable objects for now.
    constexpr uint32 kNominalUnusedBundleCapacity = 1024;

    // LOD is selected by the instance's *projected screen size*, not its distance from the camera.
    // Distance alone is blind to object scale, FOV and resolution: on a world-unit ladder a one-unit
    // prop and a cathedral at the same distance get the same LOD, so a field of small props sits at
    // the finest LOD while covering a handful of pixels each, and the raster pipeline drowns in
    // sub-pixel triangles (every triangle costs a minimum 2x2 quad however little of it is covered).
    //
    // Screen height in pixels of the instance's bounding sphere at which the finest LOD stops being
    // used. Each further boundary divides by the multiplier, so the ladder steps geometrically
    // downwards: at kLodCount 5 and the values below the boundaries are 512/171/57/19 pixels.
    // The committed LOD sticks inside a relative dead-band around each boundary (hysteresis), and
    // every switch is a dithered cross-fade over a fixed duration -- see AdvanceLodState in
    // PopulateOpaqueIndirectBundleCS.
    constexpr float32 kLodBaseScreenSize = 512.0f;
    constexpr float32 kLodScreenMultiplier = 3.0f;
    constexpr float32 kLodHysteresis = 0.1f;
    constexpr float32 kLodFadeSeconds = 0.25f;

    // A meshlet projecting to fewer than this many pixels contributes almost nothing but quad
    // overshading -- the raster shades a full 2x2 quad per triangle no matter how little of it is
    // covered. Expressed in pixels rather than as the raw NDC extent ContributionCullMeshlet
    // compares against, so it does not silently get stricter every time the resolution goes up.
    constexpr float32 kMinContributionPixels = 2.0f;

    // 16 bits of fade progress in the LOD state word -- mirrors kLodStateFadeMax in
    // PopulateOpaqueIndirectBundle.hlsl.
    constexpr float32 kLodFadeMax = 65535.0f;

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
        glm::mat4 hzbViewProjection;
        glm::mat4 prevViewProjection;
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
        uint32 rLodStateBuffer;
        float32 fLodBaseScreenSize;
        float32 fLodScreenMultiplier;
        uint32 rMaterialBuffer;
        uint32 uRegionCapacity;
        uint32 uPass;
        uint32 rVisibilityBuffer;
        uint32 uCullFlags;
        uint32 rHZB;
        uint32 uHZBWidth;
        uint32 uHZBHeight;
        uint32 uHZBMipCount;
        float32 fLodHysteresis;
        uint32 uLodFadeStep;
        float32 fLodProjScaleY;
    };

    // Mirrors BuildHZBPushConstants in Content/Shaders/BuildHZB.hlsl. The mip handles are a uint4[4]
    // there because constant-buffer arrays pad every element to 16 bytes; as a flat uint[16] on this
    // side the bytes line up exactly.
    struct BuildHZBPushConstants
    {
        uint32 rSrcDepth;
        uint32 rCounter;
        uint32 pad0[2];
        uint32 rHZBMips[16];
        uint32 uSrcDepthWidth;
        uint32 uSrcDepthHeight;
        uint32 uHZBWidth;
        uint32 uHZBHeight;
        uint32 uMipCount;
        uint32 uNumWorkGroups;
    };
}

// Mirrors ScenePushConstants in Content/Shaders/Common/SceneMesh.hlsli. Instances are submitted
// through an indirect bundle rather than one draw call per instance, so per-draw data (instance
// index, LOD, fade) is not a push constant -- the mesh shader recovers it from AGFX_DRAW_ID() (with
// the Vulkan indirection-buffer workaround, see SceneMesh.hlsli's SceneResolveDrawWord).
struct ScenePushConstants
{
    uint32 rFrameConstants;
    uint32 rInstanceBuffer;
    uint32 rMaterialBuffer;
    uint32 rSchemeParams;
    uint32 rDrawIndirection;
    uint32 rSampler;
    uint32 rFallbackTexture;
    uint32 rInstanceLodTable;
    uint32 uDrawIndirectionBase;
    uint32 uCullFlags;
    uint32 rHZB;
    uint32 uHZBWidth;
    uint32 uHZBHeight;
    uint32 uHZBMipCount;
    uint32 rInstanceVisibility;
    uint32 rMeshletVisibility;
    uint32 uMeshletVisStride;
    float32 fMinContribution;
};

// Mirrors DeferredPushConstants in Content/Shaders/Common/DeferredShading.hlsli. Shared by the three
// classification kernels and every material scheme's shading kernel -- all scalars, deliberately, so
// no field can straddle a 16-byte cbuffer boundary and silently shift everything after it.
struct DeferredPushConstants
{
    uint32 rFrameConstants;
    uint32 rInstanceBuffer;
    uint32 rMaterialBuffer;
    uint32 rSchemeParams;
    uint32 rVisibility;
    uint32 rDepth;
    uint32 rAlbedo;
    uint32 rNormal;
    uint32 rMetallicRoughness;
    uint32 rEmissive;
    uint32 rPixelList;
    uint32 rClassifyBuffer;
    uint32 rOutput;
    uint32 uSchemeId;
    uint32 uWidth;
    uint32 uHeight;
    uint32 uSchemeCount;
    // Split rather than a uint64 -- see the comment on the HLSL side. Low half is the bundle's
    // commands buffer, high half its count buffer.
    uint32 uBundleHandleLo;
    uint32 uBundleHandleHi;
};
static_assert(sizeof(DeferredPushConstants) <= 128, "Push constants are capped at 128 bytes by the root signature");

// Mirrors CompositePushConstants in Content/Shaders/Composite.hlsl.
struct CompositePushConstants
{
    uint32 rSceneLighting;
    uint32 uPassthrough;
};

// Mirrors GBufferResolvePushConstants in Content/Shaders/GBufferResolve.hlsl.
struct GBufferResolvePushConstants
{
    uint32 rFrameConstants;
    uint32 rInstanceBuffer;
    uint32 rMaterialBuffer;
    uint32 rInstanceLodTable;
    uint32 rVisibility;
    uint32 rDepth;
    uint32 rSampler;
    uint32 rFallbackTexture;
    uint32 uDebugMode;
    uint32 uWidth;
    uint32 uHeight;
};

SceneRenderer::SceneRenderer(agfx::Device& device, const SchemeRegistry& schemes, agfx::TextureFormat colorFormat,
                             agfx::TextureFormat depthFormat, uint32 framesInFlight)
    : m_Device(&device), m_Schemes(&schemes)
{
    agfx::SamplerCreateInfo samplerInfo;
    samplerInfo.SetFilter(agfx::SamplerFilter::Linear)
               .SetAddressMode(agfx::AddressMode::Repeat)
               .SetComparisonFunction(agfx::ComparisonFunction::Always)
               .SetLodRange(0.0f, 16.0f);
    m_Sampler = m_Device->CreateSampler(samplerInfo);

    agfx::RenderPipelineCreateInfo visBufferPipelineInfo;
    visBufferPipelineInfo.SetName("Scene Visibility Buffer Pipeline")
                         .SetCullMode(agfx::CullMode::Back)
                         .SetFrontFace(agfx::FrontFace::CounterClockwise)
                         .SetTopology(agfx::Topology::Triangles)
                         .SetDepthState(true, true, agfx::ComparisonFunction::Less)
                         .SetDepthFormat(depthFormat)
                         .SetSupportsIndirect(true) // required to replay this pipeline from an indirect bundle on Metal
                         .AddColorAttachment(agfx::TextureFormat::RG32Uint);
    ShaderServer::RegisterRenderPipeline(visBufferPipelineInfo, kVisBufferShaderPath);

    // Same shader, no backface culling -- replays the double-sided regions of the opaque bundle
    // (double-sided materials legitimately show their back faces).
    agfx::RenderPipelineCreateInfo twoSidedPipelineInfo = visBufferPipelineInfo;
    twoSidedPipelineInfo.SetName("Scene Visibility Buffer Two-Sided Pipeline")
                        .SetCullMode(agfx::CullMode::None);
    ShaderServer::RegisterRenderPipeline(twoSidedPipelineInfo, kVisBufferTwoSidedShaderPath);

    // Attachment order is the contract with GBufferOut in GBufferResolve.hlsl and with the
    // "GBuffer Resolve" pass in Renderer::Render -- all three must match exactly.
    agfx::RenderPipelineCreateInfo resolvePipelineInfo;
    resolvePipelineInfo.SetName("GBuffer Resolve Pipeline")
                       .SetCullMode(agfx::CullMode::None)
                       .SetFrontFace(agfx::FrontFace::CounterClockwise)
                       .SetTopology(agfx::Topology::Triangles)
                       .AddColorAttachment(kSceneLightingFormat)
                       .AddColorAttachment(agfx::TextureFormat::RGBA8Unorm)
                       .AddColorAttachment(agfx::TextureFormat::RGBA16F)
                       .AddColorAttachment(agfx::TextureFormat::RG8Unorm)
                       .AddColorAttachment(agfx::TextureFormat::RGBA16F)
                       .AddColorAttachment(agfx::TextureFormat::RG16F);
    ShaderServer::RegisterRenderPipeline(resolvePipelineInfo, kGBufferResolveShaderPath);

    agfx::ComputePipelineCreateInfo populatePipelineInfo;
    populatePipelineInfo.SetName("Populate Opaque Indirect Bundle Pipeline");
    ShaderServer::RegisterComputePipeline(populatePipelineInfo, kPopulateOpaqueIndirectBundleShaderPath);

    agfx::ComputePipelineCreateInfo buildHZBPipelineInfo;
    buildHZBPipelineInfo.SetName("Build HZB Pipeline");
    ShaderServer::RegisterComputePipeline(buildHZBPipelineInfo, kBuildHZBShaderPath);

    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("Material Classify Count Pipeline"), kClassifyCountShaderPath);
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("Material Classify Args Pipeline"), kClassifyArgsShaderPath);
    ShaderServer::RegisterComputePipeline(agfx::ComputePipelineCreateInfo().SetName("Material Classify Scatter Pipeline"), kClassifyScatterShaderPath);

    agfx::RenderPipelineCreateInfo compositePipelineInfo;
    compositePipelineInfo.SetName("Composite Pipeline")
                         .SetCullMode(agfx::CullMode::None)
                         .SetFrontFace(agfx::FrontFace::CounterClockwise)
                         .SetTopology(agfx::Topology::Triangles)
                         .AddColorAttachment(colorFormat);
    ShaderServer::RegisterRenderPipeline(compositePipelineInfo, kCompositeShaderPath);

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
        // One region per material scheme: scheme s owns command slot s and count slot s, and appends
        // at most one dispatch command (see .claude/agfx-mdi/SKILL.md gotcha 4).
        m_DeferredBundles[i] = m_Device->CreateIndirectBundle(agfx::IndirectBundleCreateInfo()
            .SetType(agfx::IndirectBundleType::Dispatch)
            .SetMaxCommandCount(SchemeRegistry::kMaxSchemes)
            .SetMaxCountCount(SchemeRegistry::kMaxSchemes));
    }

    // Resets the opaque bundle's count slots, the deferred bundle's count slots, and the classify
    // buffer, so it has to be as large as the biggest of the three.
    constexpr uint32 kZeroBufferWords = kOpaqueRegionCount > kClassifySlotCount ? kOpaqueRegionCount : kClassifySlotCount;

    agfx::BufferCreateInfo zeroBufferInfo;
    zeroBufferInfo.SetSize(kZeroBufferWords * sizeof(uint32)).SetStride(sizeof(uint32)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
    m_ZeroBuffer = m_Device->CreateBuffer(zeroBufferInfo);
    m_ZeroBuffer.SetName("Indirect Bundle Zero Buffer");
    {
        agfx::MappedBuffer mapped(m_ZeroBuffer);
        std::memset(mapped.Get(), 0, kZeroBufferWords * sizeof(uint32));
    }

    agfx::BufferCreateInfo classifyInfo;
    classifyInfo.SetSize(kClassifySlotCount * sizeof(uint32))
                .SetStride(sizeof(uint32))
                .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
                .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_ClassifyBuffer = m_Device->CreateBuffer(classifyInfo);
    m_ClassifyBuffer.SetName("Material Classify Counts");

    agfx::BufferViewCreateInfo classifyViewInfo;
    classifyViewInfo.SetBuffer(m_ClassifyBuffer.Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_ClassifyBufferView = m_Device->CreateBufferView(classifyViewInfo);

    agfx::BufferCreateInfo counterInfo;
    counterInfo.SetSize(sizeof(uint32))
               .SetStride(sizeof(uint32))
               .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
               .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_HZBCounter = m_Device->CreateBuffer(counterInfo);
    m_HZBCounter.SetName("HZB Downsampler Counter");

    agfx::BufferViewCreateInfo counterViewInfo;
    counterViewInfo.SetBuffer(m_HZBCounter.Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_HZBCounterView = m_Device->CreateBufferView(counterViewInfo);

    m_Device->MakeResourcesResident();
}

void SceneRenderer::InvalidateOcclusionState()
{
    m_HZBValid = false;
    m_VisibilityDirty = true;
}

void SceneRenderer::EnsureOpaqueCapacity(uint32 frameIndex, uint32 requiredCount)
{
    if (requiredCount <= m_OpaqueCapacities[frameIndex])
        return;

    // Overshoot so a steadily growing scene does not reallocate the bundle every single frame,
    // mirroring StreamBuffer::Reserve.
    uint32 newCapacity = requiredCount + requiredCount / 2 + 256;

    // The regions share the commands buffer (SKILL.md gotcha 4): region r occupies
    // [r * newCapacity, (r + 1) * newCapacity) and is counted in slot r.
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

    m_OpaqueCapacities[frameIndex] = newCapacity;
    m_Device->MakeResourcesResident();
}

void SceneRenderer::EnsureClassifyCapacity(uint32 width, uint32 height)
{
    // Worst case every pixel is covered, so the list is sized for the whole viewport rather than
    // grown against an observed count -- there is nothing to observe until after classification runs.
    uint32 required = width * height;
    if (required <= m_ClassifyPixelCapacity)
        return;

    agfx::BufferCreateInfo info;
    info.SetSize((uint64)required * sizeof(uint32))
        .SetStride(sizeof(uint32))
        .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
        .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_ClassifyPixelList = m_Device->CreateBuffer(info);
    m_ClassifyPixelList.SetName("Material Classify Pixel List");

    agfx::BufferViewCreateInfo viewInfo;
    viewInfo.SetBuffer(m_ClassifyPixelList.Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_ClassifyPixelListView = m_Device->CreateBufferView(viewInfo);

    m_ClassifyPixelCapacity = required;
    m_Device->MakeResourcesResident();
}

void SceneRenderer::EnsureVisibilityCapacity(uint32 requiredCount, uint32 maxMeshletCount)
{
    uint32 requiredStride = (maxMeshletCount + 31) / 32;
    if (requiredStride == 0)
        requiredStride = 1;

    if (requiredCount <= m_VisibilityCapacity && requiredStride <= m_MeshletVisStride)
        return;

    uint32 newCapacity = m_VisibilityCapacity;
    if (requiredCount > newCapacity)
        newCapacity = requiredCount + requiredCount / 2 + 256;
    uint32 newStride = requiredStride > m_MeshletVisStride ? requiredStride : m_MeshletVisStride;

    agfx::BufferCreateInfo visibilityInfo;
    visibilityInfo.SetSize((uint64)newCapacity * sizeof(uint32))
                  .SetStride(sizeof(uint32))
                  .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
                  .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_InstanceVisibility = m_Device->CreateBuffer(visibilityInfo);
    m_InstanceVisibility.SetName("Opaque Instance Visibility");

    agfx::BufferViewCreateInfo visibilityViewInfo;
    visibilityViewInfo.SetBuffer(m_InstanceVisibility.Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(true);
    m_InstanceVisibilityView = m_Device->CreateBufferView(visibilityViewInfo);

    agfx::BufferCreateInfo meshletVisInfo;
    meshletVisInfo.SetSize((uint64)newCapacity * newStride * sizeof(uint32))
                  .SetStride(sizeof(uint32))
                  .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
                  .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_MeshletVisibility = m_Device->CreateBuffer(meshletVisInfo);
    m_MeshletVisibility.SetName("Opaque Meshlet Visibility");

    agfx::BufferViewCreateInfo meshletVisViewInfo;
    meshletVisViewInfo.SetBuffer(m_MeshletVisibility.Get()).SetType(agfx::BufferViewType::Raw).SetOffset(0).SetWriteable(true);
    m_MeshletVisibilityView = m_Device->CreateBufferView(meshletVisViewInfo);

    agfx::BufferCreateInfo lodStateInfo;
    lodStateInfo.SetSize((uint64)newCapacity * sizeof(uint32))
                .SetStride(sizeof(uint32))
                .SetUsage(agfx::BufferUsage::ShaderRead | agfx::BufferUsage::ShaderWrite)
                .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
    m_LodState = m_Device->CreateBuffer(lodStateInfo);
    m_LodState.SetName("Opaque Instance LOD State");

    agfx::BufferViewCreateInfo lodStateViewInfo;
    lodStateViewInfo.SetBuffer(m_LodState.Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(true);
    m_LodStateView = m_Device->CreateBufferView(lodStateViewInfo);

    m_VisibilityCapacity = newCapacity;
    m_MeshletVisStride = newStride;
    // The fresh allocations hold nothing meaningful, so the early pass must ignore them.
    m_VisibilityDirty = true;
    m_Device->MakeResourcesResident();
}

ScenePushConstants SceneRenderer::BuildPushConstants(GPUScene& gpuScene, const HZBResources& hzb, uint32 frameIndex) const
{
    ScenePushConstants pc{};
    pc.rFrameConstants = (uint32)m_CameraBufferViews[frameIndex].GetHandle();
    pc.rInstanceBuffer = (uint32)gpuScene.GetInstanceBufferView(frameIndex).GetHandle();
    pc.rMaterialBuffer = (uint32)gpuScene.GetMaterialBufferView(frameIndex).GetHandle();
    pc.rSchemeParams = 0u;
    pc.rDrawIndirection = (uint32)m_OpaqueDrawIndirectionViews[frameIndex].GetHandle();
    pc.rSampler = (uint32)m_Sampler.GetHandle();
    pc.rFallbackTexture = gpuScene.GetFallbackTextureHandle();
    pc.rInstanceLodTable = (uint32)gpuScene.GetInstanceLodTableView(frameIndex).GetHandle();
    pc.uCullFlags = (*cv_EnableCulling.AsBoolPtr() ? kCullFlagEnabled : 0u)
                  | (m_HZBValid ? kCullFlagHZBValid : 0u);
    pc.rHZB = hzb.hzbHandle;
    pc.uHZBWidth = hzb.width;
    pc.uHZBHeight = hzb.height;
    pc.uHZBMipCount = hzb.mipCount;
    pc.rInstanceVisibility = (uint32)m_InstanceVisibilityView.GetHandle();
    pc.rMeshletVisibility = (uint32)m_MeshletVisibilityView.GetHandle();
    pc.uMeshletVisStride = m_MeshletVisStride;
    pc.fMinContribution = m_MinContribution;
    return pc;
}

// Builds the execute info for one bundle region. Also used at prepare time -- Metal bakes the
// pipeline and push constants into the ICB then (SKILL.md gotcha 2), so prepare and execute must
// receive identical infos, which sharing this helper guarantees.
agfx::IndirectBundleExecuteInfo SceneRenderer::BuildRegionExecuteInfo(const ScenePushConstants& pc, uint32 region, uint32 frameIndex) const
{
    uint32 capacity = m_OpaqueCapacities[frameIndex];
    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(
        (region & 1u) ? kVisBufferTwoSidedShaderPath : kVisBufferShaderPath, {});

    ScenePushConstants regionPC = pc;
    regionPC.uDrawIndirectionBase = region * capacity;
    // Late regions occlusion-cull meshlets against the fresh pyramid, rewrite the meshlet visibility
    // bits, and draw only what the early pass skipped; early regions draw last frame's visible
    // meshlets and never occlusion-cull. See SceneAS.
    if (region / kOpaqueRegionsPerPass == kPassLate)
        regionPC.uCullFlags |= kSceneCullFlagLatePass;

    agfx::IndirectBundleExecuteInfo info;
    info.SetCountIndex(region)
        .SetCommandRange(region * capacity, capacity)
        .SetPushConstants(regionPC)
        .SetRenderPipeline(pipeline ? pipeline->Get() : nullptr);
    return info;
}

void SceneRenderer::RecordCullPass(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const HZBResources& hzb, uint32 frameIndex, uint32 pass)
{
    uint32 count = gpuScene.GetInstanceCount();
    if (count == 0)
        return;

    // Worst case a late region holds two draws per instance while everything cross-fades at once.
    EnsureOpaqueCapacity(frameIndex, count * 2);
    // Sized by state slots, not instances: the persistent buffers are indexed by GPUInstance's
    // stateSlot so they survive Build's compaction, and freed slots stay reserved until recycled.
    EnsureVisibilityCapacity(gpuScene.GetStateSlotCount(), gpuScene.GetMaxMeshletCount());

    agfx::IndirectBundle& bundle = m_OpaqueBundles[frameIndex];
    ScenePushConstants pc = BuildPushConstants(gpuScene, hzb, frameIndex);

    // A pipeline can be momentarily missing while the shader watcher recompiles it. That must not
    // skip the count reset or the barriers below -- the render passes replay this bundle regardless,
    // and a count buffer left in COPY_DEST with no per-resource barrier on it is exactly what D3D12
    // rejects at ExecuteIndirect (it falls back to legacy state tracking for resources an enhanced
    // barrier never touched). Missing pipelines only skip the work that needs them.
    agfx::ComputePipeline* populatePipeline = ShaderServer::GetComputePipeline(kPopulateOpaqueIndirectBundleShaderPath);

    // Only the regions this pass owns.
    agfx::IndirectBundleExecuteInfo regionInfos[kOpaqueRegionsPerPass];
    bool haveRegionPipelines = true;
    for (uint32 side = 0; side < kOpaqueRegionsPerPass; ++side)
    {
        regionInfos[side] = BuildRegionExecuteInfo(pc, pass * kOpaqueRegionsPerPass + side, frameIndex);
        if (!regionInfos[side].renderPipeline)
            haveRegionPipelines = false;
    }

    // WAR: the previous use of this frame slot's bundle may still be replaying on the GPU when this
    // frame's populate work starts touching it. Per-frame-slot bundles plus the fence wait at the top
    // of Renderer::Render already rule out an actual hazard here; this is a pure state transition.
    cmd.MemoryBarrier(agfx::ResourceState::IndirectArgument, agfx::ResourceState::UnorderedAccess);

    {
        agfx::ComputePass computePass = cmd.BeginComputePass(pass == kPassEarly ? "Cull Early" : "Cull Late");

        if (pass == kPassEarly)
        {
            // All eight slots, not just this pass's four: the late cull appends into the same count
            // buffer and must not reset it, or it would drop the early pass's draws. Stale commands
            // beyond the live counts are never executed (SKILL.md gotcha 7).
            agfxComputePassCopyBufferToBuffer(computePass, m_ZeroBuffer, bundle.CountBuffer(), 0, 0, kOpaqueRegionCount * sizeof(uint32));
            cmd.MemoryBarrier(agfx::ResourceState::CopyDest, agfx::ResourceState::UnorderedAccess);
        }

        if (populatePipeline)
        {
            PopulatePushConstants populatePC{};
            populatePC.rInstanceBuffer = pc.rInstanceBuffer;
            populatePC.uInstanceCount = count;
            populatePC.uBundleHandle = bundle.GetHandle();
            populatePC.rDrawIndirection = pc.rDrawIndirection;
            populatePC.rFrameConstants = pc.rFrameConstants;
            populatePC.rInstanceLodTable = pc.rInstanceLodTable;
            populatePC.rLodStateBuffer = (uint32)m_LodStateView.GetHandle();
            populatePC.fLodBaseScreenSize = kLodBaseScreenSize;
            populatePC.fLodScreenMultiplier = kLodScreenMultiplier;
            populatePC.rMaterialBuffer = pc.rMaterialBuffer;
            populatePC.uRegionCapacity = m_OpaqueCapacities[frameIndex];
            populatePC.uPass = pass;
            populatePC.rVisibilityBuffer = (uint32)m_InstanceVisibilityView.GetHandle();
            populatePC.uCullFlags = pc.uCullFlags | (m_VisibilityDirty ? 0u : kCullFlagVisibilityValid);
            populatePC.rHZB = pc.rHZB;
            populatePC.uHZBWidth = pc.uHZBWidth;
            populatePC.uHZBHeight = pc.uHZBHeight;
            populatePC.uHZBMipCount = pc.uHZBMipCount;
            populatePC.fLodHysteresis = kLodHysteresis;
            populatePC.uLodFadeStep = m_LodFadeStep;
            populatePC.fLodProjScaleY = m_LodProjScaleY;

            computePass.SetPipeline(*populatePipeline);
            computePass.PushConstants(populatePC);
            computePass.Dispatch((count + kPopulateThreadGroupSize - 1) / kPopulateThreadGroupSize, 1, 1);
        }

        // The prepare step below reads what the populate dispatch just wrote. Emitted unconditionally:
        // these are the only per-resource barriers either bundle buffer ever gets, and D3D12 needs to
        // have seen one before it will honour the global UnorderedAccess -> IndirectArgument
        // transition at ExecuteIndirect instead of assuming a legacy state.
        computePass.BufferUAVBarrier(bundle.CommandsBuffer());
        computePass.BufferUAVBarrier(bundle.CountBuffer());

        // No-op on D3D12/Vulkan; builds the Metal ICB. Push constants/pipeline must match the
        // execute call exactly -- Metal bakes them into the ICB at this point (SKILL.md gotcha 2).
        if (haveRegionPipelines)
        {
            for (uint32 side = 0; side < kOpaqueRegionsPerPass; ++side)
                computePass.PrepareIndirectBundle(bundle, regionInfos[side]);
        }
    }

    cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::IndirectArgument);

    // Populate just wrote m_OpaqueDrawIndirection, m_LodState and m_InstanceVisibility via UAVs;
    // SceneAS reads the first in the render pass's task-shader stage, and the late cull reads the
    // other two, which is a *different* consumer sync scope than IndirectArgument above -- that
    // needs its own MemoryBarrier call, not reuse of the bundle's.
    cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::NonPixelShaderResource);

    if (pass == kPassLate)
        m_VisibilityDirty = false;
}

void SceneRenderer::CullEarly(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const HZBResources& hzb, uint32 frameIndex)
{
    RecordCullPass(cmd, gpuScene, hzb, frameIndex, kPassEarly);
}

void SceneRenderer::CullLate(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const HZBResources& hzb, uint32 frameIndex)
{
    RecordCullPass(cmd, gpuScene, hzb, frameIndex, kPassLate);
}

void SceneRenderer::BuildHZB(agfx::CommandBuffer& cmd, const HZBResources& hzb)
{
    if (!*cv_BuildHZB.AsBoolPtr())
        return;

    agfx::ComputePipeline* pipeline = ShaderServer::GetComputePipeline(kBuildHZBShaderPath);
    if (!pipeline)
        return;

    uint32 groupsX = (hzb.width + kHZBTileSize - 1) / kHZBTileSize;
    uint32 groupsY = (hzb.height + kHZBTileSize - 1) / kHZBTileSize;

    BuildHZBPushConstants pc{};
    pc.rSrcDepth = hzb.depthHandle;
    pc.rCounter = (uint32)m_HZBCounterView.GetHandle();
    for (uint32 mip = 0; mip < 16; ++mip)
        pc.rHZBMips[mip] = hzb.mipHandles[mip < kMaxHZBMips ? mip : kMaxHZBMips - 1];
    pc.uSrcDepthWidth = hzb.depthWidth;
    pc.uSrcDepthHeight = hzb.depthHeight;
    pc.uHZBWidth = hzb.width;
    pc.uHZBHeight = hzb.height;
    pc.uMipCount = hzb.mipCount;
    pc.uNumWorkGroups = groupsX * groupsY;

    {
        agfx::ComputePass computePass = cmd.BeginComputePass("Build HZB");

        // The ticket counter decides which group finishes the bottom of the pyramid, so it has to
        // start at zero every frame. It is a single buffer shared across frame slots and the previous
        // frame's dispatch left it in UnorderedAccess, so the copy needs its own transition first.
        cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::CopyDest);
        agfxComputePassCopyBufferToBuffer(computePass, m_ZeroBuffer, m_HZBCounter, 0, 0, sizeof(uint32));
        cmd.MemoryBarrier(agfx::ResourceState::CopyDest, agfx::ResourceState::UnorderedAccess);

        computePass.SetPipeline(*pipeline);
        computePass.PushConstants(pc);
        computePass.Dispatch(groupsX, groupsY, 1);
    }

    m_HZBValid = true;
}

void SceneRenderer::BeginFrame(const Camera& camera, uint32 width, uint32 height, uint32 frameIndex)
{
    // The fade advances by a fixed step per frame on the GPU, so the step has to come from real
    // delta time -- a constant per-frame increment would make the fade duration scale with FPS.
    m_FrameTimer.Tick();
    m_LodFadeStep = (uint32)glm::clamp(kLodFadeMax * m_FrameTimer.GetDelta() / kLodFadeSeconds, 1.0f, kLodFadeMax);

    float aspectRatio = height != 0 ? (float)width / (float)height : 1.0f;

    FrameConstants constants;
    constants.viewProjection = camera.GetViewProjection(aspectRatio);

    if (*cv_BuildHZB.AsBoolPtr())
        m_HZBViewProjection = constants.viewProjection;
    constants.hzbViewProjection = m_HZBViewProjection;

    // Always the live viewProjection, never the frozen-culling one: motion vectors describe what
    // was actually rendered last frame.
    constants.prevViewProjection = m_HasPrevViewProjection ? m_PrevViewProjection : constants.viewProjection;
    m_PrevViewProjection = constants.viewProjection;
    m_HasPrevViewProjection = true;

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

    // Everything the projected-size LOD metric needs that is not per instance. A sphere of radius r
    // at view depth d covers `2 * r * projScaleY / d` pixels vertically, so folding the viewport
    // height and the projection's vertical scale into one constant leaves the shader with a single
    // multiply and divide. Taken from the frozen projection so scene.freeze_frustum freezes LOD
    // selection along with culling, which is the point of the freeze.
    m_LodProjScaleY = (float32)height * 0.5f * m_FrozenProjection[1][1];

    // SphereScreenExtents reports NDC extents, and NDC spans 2.0 across the viewport, so a pixel
    // budget converts with 2 / height. Keyed on height for the same reason m_LodProjScaleY is.
    m_MinContribution = height != 0 ? 2.0f * kMinContributionPixels / (float32)height : 0.0f;

    agfx::MappedBuffer mapped(m_CameraBuffers[frameIndex]);
    std::memcpy(mapped.Get(), &constants, sizeof(constants));

    if (*cv_FreezeFrustum.AsBoolPtr())
    {
        DebugStyle style;
        style.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
        style.depthTest = false;
        DebugRenderer::Get().Frustum(m_FrozenFrustumViewProjection, style);
    }
}

void SceneRenderer::RenderEarly(agfx::RenderPass& renderPass, GPUScene& gpuScene, const HZBResources& hzb, uint32 width, uint32 height, uint32 frameIndex)
{
    uint32 count = gpuScene.GetInstanceCount();
    if (count == 0 || m_OpaqueCapacities[frameIndex] == 0)
        return;

    ScenePushConstants pc = BuildPushConstants(gpuScene, hzb, frameIndex);

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);

    // ExecuteIndirectBundle sets both the pipeline and push constants itself, from executeInfo, on
    // every backend -- no separate SetPipeline/PushConstants call here.
    for (uint32 side = 0; side < kOpaqueRegionsPerPass; ++side)
    {
        agfx::IndirectBundleExecuteInfo info = BuildRegionExecuteInfo(pc, kPassEarly * kOpaqueRegionsPerPass + side, frameIndex);
        if (info.renderPipeline)
            renderPass.ExecuteIndirectBundle(m_OpaqueBundles[frameIndex], info);
    }
}

void SceneRenderer::RenderLate(agfx::RenderPass& renderPass, GPUScene& gpuScene, const HZBResources& hzb, uint32 width, uint32 height, uint32 frameIndex)
{
    uint32 count = gpuScene.GetInstanceCount();
    if (count == 0 || m_OpaqueCapacities[frameIndex] == 0)
        return;

    ScenePushConstants pc = BuildPushConstants(gpuScene, hzb, frameIndex);

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);

    for (uint32 side = 0; side < kOpaqueRegionsPerPass; ++side)
    {
        agfx::IndirectBundleExecuteInfo info = BuildRegionExecuteInfo(pc, kPassLate * kOpaqueRegionsPerPass + side, frameIndex);
        if (info.renderPipeline)
            renderPass.ExecuteIndirectBundle(m_OpaqueBundles[frameIndex], info);
    }
}

void SceneRenderer::RenderGBufferResolve(agfx::RenderPass& renderPass, GPUScene& gpuScene, uint32 visibilityHandle, uint32 depthHandle, uint32 width, uint32 height, uint32 frameIndex)
{
    // No instances means GPUScene never created this slot's stream views, and depth stays cleared
    // so every pixel background-rejects anyway -- the pass still runs for its attachment clears.
    if (gpuScene.GetInstanceCount() == 0)
        return;

    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(kGBufferResolveShaderPath, {});
    if (!pipeline)
        return;

    GBufferResolvePushConstants pc{};
    pc.rFrameConstants = (uint32)m_CameraBufferViews[frameIndex].GetHandle();
    pc.rInstanceBuffer = (uint32)gpuScene.GetInstanceBufferView(frameIndex).GetHandle();
    pc.rMaterialBuffer = (uint32)gpuScene.GetMaterialBufferView(frameIndex).GetHandle();
    pc.rInstanceLodTable = (uint32)gpuScene.GetInstanceLodTableView(frameIndex).GetHandle();
    pc.rVisibility = visibilityHandle;
    pc.rDepth = depthHandle;
    pc.rSampler = (uint32)m_Sampler.GetHandle();
    pc.rFallbackTexture = gpuScene.GetFallbackTextureHandle();
    pc.uDebugMode = (uint32)*cv_GBufferDebug.AsIntPtr();
    pc.uWidth = width;
    pc.uHeight = height;

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);
    renderPass.SetPipeline(*pipeline);
    renderPass.PushConstants(pc);
    renderPass.Draw(3);
}

bool SceneRenderer::IsGBufferDebugActive()
{
    return *cv_GBufferDebug.AsIntPtr() != 0;
}

DeferredPushConstants SceneRenderer::BuildDeferredPushConstants(GPUScene& gpuScene, const DeferredTargets& targets,
                                                                uint32 width, uint32 height, uint32 frameIndex) const
{
    DeferredPushConstants pc{};
    pc.rFrameConstants = (uint32)m_CameraBufferViews[frameIndex].GetHandle();
    pc.rInstanceBuffer = (uint32)gpuScene.GetInstanceBufferView(frameIndex).GetHandle();
    pc.rMaterialBuffer = (uint32)gpuScene.GetMaterialBufferView(frameIndex).GetHandle();
    pc.rSchemeParams = 0; // per-scheme, filled by BuildSchemeExecuteInfo
    pc.rVisibility = targets.visibilityHandle;
    pc.rDepth = targets.depthHandle;
    pc.rAlbedo = targets.albedoHandle;
    pc.rNormal = targets.normalHandle;
    pc.rMetallicRoughness = targets.metallicRoughnessHandle;
    pc.rEmissive = targets.emissiveHandle;
    pc.rPixelList = (uint32)m_ClassifyPixelListView.GetHandle();
    pc.rClassifyBuffer = (uint32)m_ClassifyBufferView.GetHandle();
    pc.rOutput = targets.sceneLightingUAVHandle;
    pc.uSchemeId = 0;
    pc.uWidth = width;
    pc.uHeight = height;
    pc.uSchemeCount = m_Schemes->Count();

    uint64 bundleHandle = m_DeferredBundles[frameIndex].GetHandle();
    pc.uBundleHandleLo = (uint32)(bundleHandle & 0xFFFFFFFFull);
    pc.uBundleHandleHi = (uint32)(bundleHandle >> 32);
    return pc;
}

agfx::IndirectBundleExecuteInfo SceneRenderer::BuildSchemeExecuteInfo(const DeferredPushConstants& pc, GPUScene& gpuScene,
                                                                      uint32 schemeId, uint32 frameIndex) const
{
    const MaterialScheme& scheme = m_Schemes->Get(schemeId);

    DeferredPushConstants schemePC = pc;
    schemePC.uSchemeId = schemeId;

    // The view is null both when the scheme declares no parameters and when no material slot used it
    // this frame -- GPUScene::UploadSchemeParams skips a scheme with empty staging, so paramStride
    // alone does not prove a buffer exists. Either way the kernel never reads the handle: a scheme
    // with no slots has no pixels, so its count slot is 0 and it replays no dispatch at all.
    agfx::BufferView& paramView = gpuScene.GetSchemeParamBufferView(schemeId, frameIndex);
    schemePC.rSchemeParams = (scheme.paramStride > 0 && paramView) ? (uint32)paramView.GetHandle() : 0u;

    agfx::ComputePipeline* pipeline = ShaderServer::GetComputePipeline(scheme.shaderPath, {});

    agfx::IndirectBundleExecuteInfo info;
    info.SetCountIndex(schemeId)
        .SetCommandRange(schemeId, 1)   // one dispatch command per scheme
        .SetPushConstants(schemePC)
        .SetComputePipeline(pipeline ? pipeline->Get() : nullptr);
    return info;
}

void SceneRenderer::ClassifyMaterials(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const DeferredTargets& targets,
                                      uint32 width, uint32 height, uint32 frameIndex)
{
    // A debug view already owns the lighting target, and shading would only overwrite it.
    if (IsGBufferDebugActive() || gpuScene.GetInstanceCount() == 0)
        return;

    // DeferredPackPixel packs a coordinate into 16 bits per axis. Unreachable in practice, but
    // silently wrapping would scatter pixels to the wrong place rather than fail visibly.
    if (width > kMaxClassifiedViewportExtent || height > kMaxClassifiedViewportExtent)
    {
        if (!m_WarnedClassifyExtent)
        {
            CARAMEL_ERROR("SceneRenderer: viewport {}x{} exceeds the {}-pixel limit the material classification pixel list packs into, deferred shading disabled",
                          width, height, kMaxClassifiedViewportExtent);
            m_WarnedClassifyExtent = true;
        }
        return;
    }

    EnsureClassifyCapacity(width, height);

    agfx::IndirectBundle& bundle = m_DeferredBundles[frameIndex];
    DeferredPushConstants pc = BuildDeferredPushConstants(gpuScene, targets, width, height, frameIndex);

    // Null while the shader watcher recompiles. As in RecordCullPass, that must not skip the count
    // reset or the barriers -- the shade pass replays this bundle regardless, and D3D12 rejects
    // ExecuteIndirect on a count buffer left in COPY_DEST with no per-resource barrier on it.
    agfx::ComputePipeline* countPipeline = ShaderServer::GetComputePipeline(kClassifyCountShaderPath, {});
    agfx::ComputePipeline* argsPipeline = ShaderServer::GetComputePipeline(kClassifyArgsShaderPath, {});
    agfx::ComputePipeline* scatterPipeline = ShaderServer::GetComputePipeline(kClassifyScatterShaderPath, {});

    uint32 schemeCount = m_Schemes->Count();
    TArray<agfx::IndirectBundleExecuteInfo> schemeInfos;
    bool haveSchemePipelines = schemeCount > 0;
    for (uint32 scheme = 0; scheme < schemeCount; ++scheme)
    {
        schemeInfos.PushBack(BuildSchemeExecuteInfo(pc, gpuScene, scheme, frameIndex));
        if (!schemeInfos[scheme].computePipeline)
            haveSchemePipelines = false;
    }

    // WAR: the previous use of this frame slot's bundle may still be replaying. Per-frame-slot
    // bundles plus the fence wait in Renderer::Render rule out a real hazard; this is a pure state
    // transition, same as RecordCullPass's.
    cmd.MemoryBarrier(agfx::ResourceState::IndirectArgument, agfx::ResourceState::UnorderedAccess);

    {
        agfx::ComputePass computePass = cmd.BeginComputePass("Material Classify");

        agfxComputePassCopyBufferToBuffer(computePass, m_ZeroBuffer, m_ClassifyBuffer, 0, 0, kClassifySlotCount * sizeof(uint32));
        agfxComputePassCopyBufferToBuffer(computePass, m_ZeroBuffer, bundle.CountBuffer(), 0, 0, schemeCount * sizeof(uint32));
        cmd.MemoryBarrier(agfx::ResourceState::CopyDest, agfx::ResourceState::UnorderedAccess);

        uint32 groupsX = (width + kClassifyTileSize - 1) / kClassifyTileSize;
        uint32 groupsY = (height + kClassifyTileSize - 1) / kClassifyTileSize;

        if (countPipeline)
        {
            computePass.SetPipeline(*countPipeline);
            computePass.PushConstants(pc);
            computePass.Dispatch(groupsX, groupsY, 1);
        }

        computePass.BufferUAVBarrier(m_ClassifyBuffer);

        // Turns the counts into pixel-list offsets and appends the dispatch commands. Single group:
        // the whole working set is a handful of uints.
        if (argsPipeline)
        {
            computePass.SetPipeline(*argsPipeline);
            computePass.PushConstants(pc);
            computePass.Dispatch(1, 1, 1);
        }

        computePass.BufferUAVBarrier(m_ClassifyBuffer);

        if (scatterPipeline)
        {
            computePass.SetPipeline(*scatterPipeline);
            computePass.PushConstants(pc);
            computePass.Dispatch(groupsX, groupsY, 1);
        }

        // Unconditional, for the same D3D12 reason as RecordCullPass: these are the only
        // per-resource barriers either bundle buffer ever gets.
        computePass.BufferUAVBarrier(bundle.CommandsBuffer());
        computePass.BufferUAVBarrier(bundle.CountBuffer());

        // No-op on D3D12/Vulkan; builds the Metal ICB. Push constants and pipeline must match the
        // execute call exactly, which is why both go through BuildSchemeExecuteInfo.
        if (haveSchemePipelines)
        {
            for (uint32 scheme = 0; scheme < schemeCount; ++scheme)
                computePass.PrepareIndirectBundle(bundle, schemeInfos[scheme]);
        }
    }

    cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::IndirectArgument);

    // The classify buffer and pixel list are read by the shading kernels as plain buffers, which is a
    // different consumer sync scope than IndirectArgument above -- it needs its own call.
    cmd.MemoryBarrier(agfx::ResourceState::UnorderedAccess, agfx::ResourceState::NonPixelShaderResource);
}

void SceneRenderer::ShadeMaterials(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const DeferredTargets& targets,
                                   uint32 width, uint32 height, uint32 frameIndex)
{
    if (IsGBufferDebugActive() || gpuScene.GetInstanceCount() == 0 || m_ClassifyPixelCapacity == 0)
        return;

    DeferredPushConstants pc = BuildDeferredPushConstants(gpuScene, targets, width, height, frameIndex);

    agfx::ComputePass computePass = cmd.BeginComputePass("Material Shade");

    // One dispatch command per scheme, each sized by that scheme's pixel count. A scheme with no
    // pixels has a count slot of 0 and replays nothing.
    for (uint32 scheme = 0; scheme < m_Schemes->Count(); ++scheme)
    {
        agfx::IndirectBundleExecuteInfo info = BuildSchemeExecuteInfo(pc, gpuScene, scheme, frameIndex);
        if (info.computePipeline)
            computePass.ExecuteIndirectBundle(m_DeferredBundles[frameIndex], info);
    }
}

void SceneRenderer::RenderComposite(agfx::RenderPass& renderPass, uint32 sceneLightingHandle, uint32 width, uint32 height)
{
    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(kCompositeShaderPath, {});
    if (!pipeline)
        return;

    CompositePushConstants pc{};
    pc.rSceneLighting = sceneLightingHandle;
    pc.uPassthrough = IsGBufferDebugActive() ? 1u : 0u;

    renderPass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
    renderPass.SetScissor(0, 0, width, height);
    renderPass.SetPipeline(*pipeline);
    renderPass.PushConstants(pc);
    renderPass.Draw(3);
}
