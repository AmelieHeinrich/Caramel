/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Core/Timer.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/Camera.hpp>
#include <Caramel/Renderer/MaterialScheme.hpp>
#include <Caramel/Scene/GPUScene.hpp>

#include <AGFX/agfx.hpp>

struct ScenePushConstants;
struct DeferredPushConstants;

/// @brief Two-pass HZB occlusion culling at meshlet granularity. Frame order is Cull Early ->
/// Scene Early -> Build HZB -> Cull Late -> Scene Late. The early pass redraws the meshlets the late
/// pass marked visible last frame, the pyramid is rebuilt from the depth that produced, and the late
/// pass re-tests every meshlet of every visible instance against the fresh pyramid, drawing only what
/// the early pass skipped. Anything that becomes disoccluded therefore appears the same frame rather
/// than one frame late, and occlusion tests never run against a pyramid from another viewpoint --
/// see SceneMesh.hlsli's SceneAS and OcclusionCullMeshlet.
class SceneRenderer
{
public:
    SceneRenderer(agfx::Device& device, const SchemeRegistry& schemes, agfx::TextureFormat colorFormat,
                  agfx::TextureFormat depthFormat, uint32 framesInFlight);

    /// @brief Uploads this frame's camera constants and issues the frozen-frustum debug draw. Must
    /// run before the render graph is built -- both cull dispatches read what it writes.
    void BeginFrame(const Camera& camera, uint32 width, uint32 height, uint32 frameIndex);

    /// @brief Populates bundle regions 0/1 from last frame's visibility flags, and resets all four
    /// count slots (the late cull appends into the same count buffer without a second reset).
    void CullEarly(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const HZBResources& hzb, uint32 frameIndex);

    /// @brief Rebuilds the whole depth pyramid from the early pass's depth in a single dispatch.
    void BuildHZB(agfx::CommandBuffer& cmd, const HZBResources& hzb);

    /// @brief Populates bundle regions 2/3 with everything visible against the fresh pyramid that the
    /// early pass did not already draw, and refreshes the visibility flags for next frame.
    void CullLate(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const HZBResources& hzb, uint32 frameIndex);

    void RenderEarly(agfx::RenderPass& renderPass, GPUScene& gpuScene, const HZBResources& hzb, uint32 width, uint32 height, uint32 frameIndex);
    void RenderLate(agfx::RenderPass& renderPass, GPUScene& gpuScene, const HZBResources& hzb, uint32 width, uint32 height, uint32 frameIndex);

    /// @brief Fullscreen visibility-buffer resolve: reads the visibility + depth targets and writes
    /// the whole gbuffer. Attachment 0 is the scene lighting buffer, which this pass only clears (or
    /// fills with the scene.gbuffer_debug view) -- ShadeMaterials is what actually shades it.
    void RenderGBufferResolve(agfx::RenderPass& renderPass, GPUScene& gpuScene, uint32 visibilityHandle, uint32 depthHandle, uint32 width, uint32 height, uint32 frameIndex);

    /// @brief Bins every covered pixel by its material's scheme id and builds one indirect dispatch
    /// command per scheme: count -> prefix sum -> scatter -> args (Notes/GPU-Driven.md). No-ops while
    /// a scene.gbuffer_debug view is selected, since ShadeMaterials would overwrite it.
    void ClassifyMaterials(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const DeferredTargets& targets, uint32 width, uint32 height, uint32 frameIndex);

    /// @brief Replays the classification's dispatch commands, one per scheme, each running that
    /// scheme's compute kernel over only the pixels it owns.
    void ShadeMaterials(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const DeferredTargets& targets, uint32 width, uint32 height, uint32 frameIndex);

    /// @brief Fullscreen resolve of the HDR lighting buffer into scene color.
    void RenderComposite(agfx::RenderPass& renderPass, uint32 sceneLightingHandle, uint32 width, uint32 height);

    /// @brief True while a scene.gbuffer_debug view is selected, i.e. classification and shading are
    /// skipped and GBuffer Resolve's own output is what reaches the screen.
    static bool IsGBufferDebugActive();

    /// @brief Call when the HZB is recreated (viewport resize): the new pyramid holds garbage and the
    /// visibility flags describe a projection that no longer exists.
    void InvalidateOcclusionState();

private:
    ScenePushConstants BuildPushConstants(GPUScene& gpuScene, const HZBResources& hzb, uint32 frameIndex) const;

    /// @brief Execute info for one region of the opaque bundle. Shared between PrepareIndirectBundle
    /// and ExecuteIndirectBundle so their pipeline + push constants can never drift (Metal bakes both
    /// into the ICB at prepare time).
    agfx::IndirectBundleExecuteInfo BuildRegionExecuteInfo(const ScenePushConstants& pc, uint32 region, uint32 frameIndex) const;

    /// @brief Everything the classify and shade kernels share, minus the per-scheme fields.
    DeferredPushConstants BuildDeferredPushConstants(GPUScene& gpuScene, const DeferredTargets& targets,
                                                     uint32 width, uint32 height, uint32 frameIndex) const;

    /// @brief Execute info for one scheme's region of the deferred bundle. Shared between
    /// ClassifyMaterials (which prepares) and ShadeMaterials (which executes) for the same reason as
    /// BuildRegionExecuteInfo: Metal bakes the push constants into the ICB at prepare time.
    agfx::IndirectBundleExecuteInfo BuildSchemeExecuteInfo(const DeferredPushConstants& pc, GPUScene& gpuScene,
                                                           uint32 schemeId, uint32 frameIndex) const;

    void EnsureOpaqueCapacity(uint32 frameIndex, uint32 requiredCount);

    /// @brief Grows the per-scheme pixel list to cover the viewport.
    void EnsureClassifyCapacity(uint32 width, uint32 height);
    void EnsureVisibilityCapacity(uint32 requiredCount, uint32 maxMeshletCount);

    /// @brief The half of a cull dispatch that is identical between the two passes.
    void RecordCullPass(agfx::CommandBuffer& cmd, GPUScene& gpuScene, const HZBResources& hzb, uint32 frameIndex, uint32 pass);

    agfx::Device* m_Device;

    // Read for the scheme count and each scheme's shader path. Owned by Renderer and outlives this.
    const SchemeRegistry* m_Schemes;

    agfx::Sampler m_Sampler;

    agfx::Buffer m_CameraBuffers[FRAMES_IN_FLIGHT];
    agfx::BufferView m_CameraBufferViews[FRAMES_IN_FLIGHT];

    // Indirect submission. Opaque is the only bundle actually populated/executed today (visbuffer
    // pass doesn't exist yet); Translucent (forward) and Deferred (compute dispatch) are created so
    // the workflow holds all three, and stay unused until their passes exist.
    agfx::IndirectBundle m_OpaqueBundles[FRAMES_IN_FLIGHT];
    agfx::IndirectBundle m_TranslucentBundles[FRAMES_IN_FLIGHT];
    agfx::IndirectBundle m_DeferredBundles[FRAMES_IN_FLIGHT];

    // Vulkan-only workaround (SKILL.md gotcha 6): maps a bundle slot to the draw word the populate
    // shader wrote there, since Vulkan's AGFX_DRAW_ID() returns the linear slot instead.
    agfx::Buffer m_OpaqueDrawIndirection[FRAMES_IN_FLIGHT];
    agfx::BufferView m_OpaqueDrawIndirectionViews[FRAMES_IN_FLIGHT];

    uint32 m_OpaqueCapacities[FRAMES_IN_FLIGHT] = {};

    // Material classification state. Three parallel per-scheme uint arrays in one allocation --
    // counts, pixel-list offsets, scatter cursors -- laid out by kClassify*Base in
    // Common/DeferredShading.hlsli. Raw views because only AGFXRWByteAddressBuffer carries the
    // Interlocked* family.
    //
    // Deliberately not per-frame-slot, unlike the bundles: nothing reads them across frames, they are
    // zeroed at the top of every classify pass, and the fence wait in Renderer::Render already keeps
    // frame N+1's classify off frame N's shading.
    agfx::Buffer m_ClassifyBuffer;
    agfx::BufferView m_ClassifyBufferView;

    // One uint per covered pixel, (y << 16) | x, grouped into a contiguous run per scheme. Sized for
    // the whole viewport since in the worst case every pixel is covered.
    agfx::Buffer m_ClassifyPixelList;
    agfx::BufferView m_ClassifyPixelListView;
    uint32 m_ClassifyPixelCapacity = 0;
    bool m_WarnedClassifyExtent = false;

    // Per-instance visibility, one uint per instance. Deliberately *not* per-frame-slot: the whole
    // point is that frame N's early pass reads what frame N-1's late pass wrote. Within a frame the
    // early cull overwrites it with what it actually drew, so the late cull can subtract that exactly
    // instead of re-deriving it.
    //
    // GPUScene::Build compacts instances by residency, so an index can change meaning when streaming
    // state does. Worst case is one frame of misattributed flags, which the late pass corrects the
    // same frame -- not worth defending against.
    agfx::Buffer m_InstanceVisibility;
    agfx::BufferView m_InstanceVisibilityView;
    uint32 m_VisibilityCapacity = 0;

    // Per-meshlet visibility bits, m_MeshletVisStride words per instance, bit m of word m/32 =
    // "meshlet m passed every late-pass test last time its instance was emitted late". Same persistent
    // single-allocation lifecycle as m_InstanceVisibility, and only meaningful together with the
    // per-instance flags (kInstanceVisBitsLodValid guards LOD changes and staleness).
    agfx::Buffer m_MeshletVisibility;
    agfx::BufferView m_MeshletVisibilityView;
    uint32 m_MeshletVisStride = 0;

    // Per-instance LOD hysteresis/cross-fade state word (layout in PopulateOpaqueIndirectBundle.hlsl),
    // advanced once per frame by the early cull pass. Same persistent single-allocation lifecycle
    // and validity guard as m_InstanceVisibility.
    agfx::Buffer m_LodState;
    agfx::BufferView m_LodStateView;

    // Measures real frame delta so the cross-fade lasts kLodFadeSeconds regardless of frame rate.
    Timer m_FrameTimer;
    uint32 m_LodFadeStep = 0xFFFFu;

    // viewportHeight * 0.5 * projection[1][1] -- converts a world-space radius over view depth into
    // a height in pixels, which is what the LOD ladder is keyed on. See BeginFrame.
    float32 m_LodProjScaleY = 1.0f;

    // kMinContributionPixels converted into the NDC extent ContributionCullMeshlet compares against.
    float32 m_MinContribution = 0.0f;

    // The downsampler's cross-workgroup ticket counter. Zeroed from m_ZeroBuffer every frame.
    agfx::Buffer m_HZBCounter;
    agfx::BufferView m_HZBCounterView;

    // Set until a pyramid has actually been built into the current HZB texture, and until the
    // visibility flags describe the current instance set. Both reach the shaders as uCullFlags bits
    // rather than as skipped work: the early cull still dispatches, it just draws nothing and zeroes
    // the flags, leaving the late pass to rebuild everything from scratch. Skipping the dispatch
    // outright would also skip its bundle-buffer barriers, which D3D12 rejects at ExecuteIndirect.
    bool m_HZBValid = false;
    bool m_VisibilityDirty = true;

    // Single persistent zero source used to reset the bundle's count slots and the downsampler's
    // counter every frame (the commands buffer needs no reset -- see SKILL.md gotcha 7).
    agfx::Buffer m_ZeroBuffer;

    // Every FrameConstants field the camera drives, held in place while scene.freeze_frustum is on
    // instead of tracking the live camera every frame -- everything except viewProjection, which
    // BeginFrame always recomputes live so the scene still renders correctly from wherever the editor
    // camera currently is. That split is what lets contribution/frustum/cone culling be inspected
    // from outside the frozen viewpoint: culling reads the frozen fields, rendering reads the live one.
    // Occlusion culling is the exception -- it must project with the camera the pyramid it samples was
    // rasterized from, which is m_HZBViewProjection below, not any of these.
    glm::mat4 m_FrozenView{ 1.0f };
    glm::mat4 m_FrozenProjection{ 1.0f };
    glm::mat4 m_FrozenInvView{ 1.0f };
    glm::mat4 m_FrozenInvProjection{ 1.0f };
    glm::mat4 m_FrozenInvViewProjection{ 1.0f };
    glm::vec4 m_FrozenFrustumPlanes[6] = {};
    glm::vec3 m_FrozenCameraPosition{ 0.0f };
    float m_FrozenNearPlane = 0.0f;
    float m_FrozenFarPlane = 0.0f;

    glm::mat4 m_FrozenFrustumViewProjection{ 1.0f };
    glm::mat4 m_HZBViewProjection{ 1.0f };

    glm::mat4 m_PrevViewProjection{ 1.0f };
    bool m_HasPrevViewProjection = false;
};
