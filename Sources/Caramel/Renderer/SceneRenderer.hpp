/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/Camera.hpp>
#include <Caramel/Scene/GPUScene.hpp>

#include <AGFX/agfx.hpp>

struct ScenePushConstants;

class SceneRenderer
{
public:
    SceneRenderer(agfx::Device& device, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat, uint32 framesInFlight);

    /// @brief Populates the Opaque indirect bundle for this frame (grow-on-demand, populate compute
    /// dispatch, PrepareIndirectBundle) and transitions it into IndirectArgument state. Must run in
    /// its own render-graph pass before Render()'s render pass, on the same command buffer.
    void PrepareIndirectBundles(agfx::CommandBuffer& cmd, GPUScene& gpuScene, uint32 frameIndex);

    void Render(agfx::RenderPass& renderPass, GPUScene& gpuScene,
                const Camera& camera, uint32 width, uint32 height, uint32 frameIndex);

private:
    ScenePushConstants BuildPushConstants(GPUScene& gpuScene, uint32 frameIndex) const;

    /// @brief Execute info for one region of the opaque bundle (0 = single-sided/backface-culling
    /// pipeline, 1 = double-sided/no-culling). Shared between PrepareIndirectBundle and
    /// ExecuteIndirectBundle so their pipeline + push constants can never drift (Metal bakes both
    /// into the ICB at prepare time).
    agfx::IndirectBundleExecuteInfo BuildRegionExecuteInfo(const ScenePushConstants& pc, uint32 region, uint32 frameIndex) const;

    void EnsureOpaqueCapacity(uint32 frameIndex, uint32 requiredCount);

    agfx::Device* m_Device;

    agfx::Sampler m_Sampler;

    agfx::Buffer m_CameraBuffers[FRAMES_IN_FLIGHT];
    agfx::BufferView m_CameraBufferViews[FRAMES_IN_FLIGHT];

    // Indirect submission. Opaque is the only bundle actually populated/executed today (visbuffer
    // pass doesn't exist yet); Translucent (forward) and Deferred (compute dispatch) are created so
    // the workflow holds all three, and stay unused until their passes exist.
    agfx::IndirectBundle m_OpaqueBundles[FRAMES_IN_FLIGHT];
    agfx::IndirectBundle m_TranslucentBundles[FRAMES_IN_FLIGHT];
    agfx::IndirectBundle m_DeferredBundles[FRAMES_IN_FLIGHT];

    // Vulkan-only workaround (SKILL.md gotcha 6): maps a bundle slot to the instance index the
    // populate shader wrote there, since Vulkan's AGFX_DRAW_ID() returns the linear slot instead.
    agfx::Buffer m_OpaqueDrawIndirection[FRAMES_IN_FLIGHT];
    agfx::BufferView m_OpaqueDrawIndirectionViews[FRAMES_IN_FLIGHT];

    // Per-instance LOD chosen this frame by the populate compute shader, stateless/no hysteresis.
    // Same growth profile as m_OpaqueDrawIndirection (one entry per instance slot); grown alongside
    // it in EnsureOpaqueCapacity. Read by SceneAS in the later render pass.
    agfx::Buffer m_SelectedLod[FRAMES_IN_FLIGHT];
    agfx::BufferView m_SelectedLodViews[FRAMES_IN_FLIGHT];

    uint32 m_OpaqueCapacities[FRAMES_IN_FLIGHT] = {};

    // Single persistent zero source used to reset a bundle's count slot every frame (its commands
    // buffer needs no reset -- see SKILL.md gotcha 7).
    agfx::Buffer m_ZeroBuffer;

    // Every FrameConstants field the camera drives, held in place while scene.freeze_frustum is on
    // instead of tracking the live camera every frame -- everything except viewProjection, which
    // Render() always recomputes live so the scene still renders correctly from wherever the editor
    // camera currently is. That split is what lets contribution/frustum/cone culling be inspected
    // from outside the frozen viewpoint: culling reads the frozen fields, rendering reads the live one.
    glm::mat4 m_FrozenView{ 1.0f };
    glm::mat4 m_FrozenProjection{ 1.0f };
    glm::mat4 m_FrozenInvView{ 1.0f };
    glm::mat4 m_FrozenInvProjection{ 1.0f };
    glm::mat4 m_FrozenInvViewProjection{ 1.0f };
    glm::vec4 m_FrozenFrustumPlanes[6] = {};
    glm::vec3 m_FrozenCameraPosition{ 0.0f };
    float m_FrozenNearPlane = 0.0f;
    float m_FrozenFarPlane = 0.0f;

    // The frozen frustum's own view*projection, used only to debug-draw its wireframe -- distinct
    // from the live viewProjection uploaded to the GPU for actual rendering.
    glm::mat4 m_FrozenFrustumViewProjection{ 1.0f };
};
