/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

class SceneRenderer;

// The two-pass HZB occlusion culling chain: Cull Early -> Scene Early -> Build HZB -> Cull Late ->
// Scene Late. Registered as one RenderPass rather than five because they are not five independent
// techniques -- they share the indirect bundles, the per-instance/per-meshlet visibility buffers and
// the LOD state, and the early pass's correctness depends on what the late pass wrote last frame.
// Splitting them into five classes would just be five pointers back to the same state blob.
//
// The actual recording still lives in SceneRenderer; this owns the graph declarations only.
class VisibilityPasses : public RenderPass
{
public:
    explicit VisibilityPasses(SceneRenderer& sceneRenderer) : m_SceneRenderer(&sceneRenderer) {}

    const char* Name() const override { return "Visibility"; }

    // Uploads camera constants. Must run before the graph is built -- both cull dispatches read them.
    void BeginFrame(FrameContext& ctx) override;

    void Register(RenderGraph& graph, FrameContext& ctx) override;

private:
    SceneRenderer* m_SceneRenderer;
};
