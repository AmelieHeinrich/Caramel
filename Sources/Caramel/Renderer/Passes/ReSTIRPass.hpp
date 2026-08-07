/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

#include <AGFX/agfx.hpp>

/// @brief Direct lighting through ReSTIR, as an alternative to the per-scheme visibility dispatch in
/// DeferredShadingPasses. Both write ctx.sceneLighting, so exactly one of the two runs per frame --
/// scene.restir picks which.
///
/// Four dispatches, in one compute pass, one screen-space thread each:
///   1. Initial Sample -- RIS over M candidates drawn from the pixel's light list.
///   2. Temporal Reuse -- combine the reservoir this surface held last frame, found by reprojecting
///                        through the motion gbuffer.
///   3. Spatial Reuse  -- combine k neighbouring reservoirs that pass a geometric similarity test.
///   4. Shade          -- evaluate the surviving sample's BRDF and write the lighting buffer.
///
/// @warning STAGES 1-3 ARE SCAFFOLDING. They compile and dispatch but their bodies are empty, and no
/// reservoir buffer is allocated yet -- rReservoirsIn/rReservoirsOut are 0 and must not be read
/// unguarded. Stage 4 carries a real brute-force loop, identical to what DefaultPBR's kernel does,
/// so toggling scene.restir on today gives a correct image to diff the reuse math against as it
/// lands rather than a black screen. See Content/Shaders/Common/ReSTIR.hlsli.
///
/// Standalone by design: it reads the gbuffer directly and hardcodes a Cook-Torrance BRDF instead of
/// going through material classification. The cost is that a Toon material shades as PBR while this
/// is on; the benefit is that the reservoir stages are not constrained by the indirect bundle, whose
/// push constants Metal bakes into an ICB at classify time. The gbuffer decode and the light-list
/// walk are still shared with the scheme path (GBufferSurface.hlsli / LightList.hlsli), which is
/// what keeps the two from disagreeing about the surface or the candidate set.
class ReSTIRPass : public RenderPass
{
public:
    explicit ReSTIRPass(agfx::Device& device);

    const char* Name() const override { return "ReSTIR DI"; }

    /// @brief Whether ReSTIR owns the shading this frame. Static so DeferredShadingPasses can ask
    /// without holding a pointer to us -- same shape as SceneRenderer::IsGBufferDebugActive.
    static bool IsShadingActive();

    /// @brief Advances the monotonic frame counter the stages seed their RNG from. Not ctx.frameIndex,
    /// which wraps every FRAMES_IN_FLIGHT frames and would make the sample sequence repeat.
    void BeginFrame(FrameContext& ctx) override;

    bool Enabled(const FrameContext& ctx) const override;

    void Register(RenderGraph& graph, FrameContext& ctx) override;

private:
    agfx::Device* m_Device;

    uint32 m_FrameNumber = 0;
};
