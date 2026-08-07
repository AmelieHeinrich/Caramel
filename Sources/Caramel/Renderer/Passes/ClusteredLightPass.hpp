/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

#include <AGFX/agfx.hpp>

/// @brief Bins the scene's lights into a view-frustum cluster grid so the shading kernels evaluate
/// only the lights that can actually reach a pixel, instead of every light in the scene.
///
/// Three dispatches, in one compute pass:
///   1. Cull Lights   -- every light against the camera frustum; directional lights are split off
///                       into their own list, since they are unbounded and reach every pixel.
///   2. Build Clusters-- one view-space AABB per cluster (16x9x24, exponential depth slices).
///   3. Cull Clusters -- the frustum survivors against every cluster, appended into fixed-stride
///                       per-cluster index lists.
///
/// Unlike the other passes this one owns its pipelines and buffers outright rather than delegating
/// to SceneRenderer: none of it is shared with anything else, and the render graph cannot own the
/// buffers either (RGResolveContext hands out bindless *texture* handles only), so they are plain
/// agfx objects with a manual barrier sequence -- the same shape as SceneRenderer's classify buffers.
///
/// Must be registered before GBuffer Resolve, which reads the grid for its heatmap debug view, and
/// therefore before Material Classify -- which on Metal bakes the shading push constants (carrying
/// this grid's handles) into the ICB at prepare time, so they have to be final by then.
class ClusteredLightPass : public RenderPass
{
public:
    explicit ClusteredLightPass(agfx::Device& device);

    const char* Name() const override { return "Cluster Lights"; }

    /// @brief Sizes the visible-light list for this frame's light count, derives the depth-slice
    /// constants from the camera, and refreshes the handle set. Runs before the graph is built
    /// because Material Classify reads those handles at registration time, not at execute time.
    void BeginFrame(FrameContext& ctx) override;

    void Register(RenderGraph& graph, FrameContext& ctx) override;

    /// @brief What the shading kernels and the heatmap need. Valid from the first BeginFrame.
    const ClusterResources& GetResources() const { return m_Resources; }

private:
    /// @brief Grows the compacted frustum-survivor list to hold every light in the scene. The grid
    /// buffers themselves never grow -- the cluster count is a compile-time constant.
    void EnsureVisibleLightCapacity(uint32 lightCount);

    /// @brief Reads this frame slot's counter copy -- a few frames old -- and warns once per kind of
    /// overflow. Three words, mapped directly, no staging.
    void ReportOverflow(uint32 frameIndex);

    agfx::Device* m_Device;

    // View-space AABB per cluster. Rebuilt every frame rather than cached against the projection --
    // it is 3456 threads, and a cache is one more thing to be stale when the camera changes.
    agfx::Buffer m_ClusterAABBs;
    agfx::BufferView m_ClusterAABBsView;

    // Per-cluster counts followed by per-cluster index lists, one allocation, laid out by
    // kCluster*Base in Content/Shaders/Common/ClusteredLights.hlsli. Raw view because only
    // AGFXRWByteAddressBuffer carries the Interlocked* family.
    //
    // Deliberately not per-frame-slot, for the same reason as SceneRenderer's classify buffers:
    // nothing reads them across frames, they are rewritten from scratch every frame, and the fence
    // wait in Renderer::Render already keeps frame N+1's culling off frame N's shading.
    agfx::Buffer m_ClusterLights;
    agfx::BufferView m_ClusterLightsView;

    // The frustum pass's counters plus the directional index list.
    agfx::Buffer m_LightCull;
    agfx::BufferView m_LightCullView;

    // One readback copy of m_LightCull's counters per frame-in-flight slot. Per-slot because that is
    // what makes the map safe with no extra synchronisation: the fence wait at the top of
    // Renderer::Render proves slot S's GPU work from FRAMES_IN_FLIGHT frames ago has retired, and
    // BeginFrame runs after it. The numbers are therefore a few frames stale, which is fine for a
    // capacity warning.
    agfx::Buffer m_CounterReadback[FRAMES_IN_FLIGHT];

    // Frustum survivors, compacted. The only buffer here that depends on scene content.
    agfx::Buffer m_VisibleLights;
    agfx::BufferView m_VisibleLightsView;
    uint32 m_VisibleLightCapacity = 0;

    // Resets m_LightCull's three counters. The per-cluster counts need no equivalent: Build Clusters
    // already runs one thread per cluster and zeroes its own.
    agfx::Buffer m_ZeroBuffer;

    ClusterResources m_Resources{};
    uint32 m_LightCount = 0;

    bool m_WarnedDirectionalOverflow = false;
    bool m_WarnedClusterOverflow = false;
};
