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
    agfx::IndirectBundleExecuteInfo BuildExecuteInfo(const ScenePushConstants& pc, uint32 commandCount) const;

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
    uint32 m_OpaqueCapacities[FRAMES_IN_FLIGHT] = {};

    // Single persistent zero source used to reset a bundle's count slot every frame (its commands
    // buffer needs no reset -- see SKILL.md gotcha 7).
    agfx::Buffer m_ZeroBuffer;
};
