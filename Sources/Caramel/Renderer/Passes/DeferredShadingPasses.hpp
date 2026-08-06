/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

class SceneRenderer;

// Material classification followed by per-scheme shading. One RenderPass rather than two because
// Classify prepares the very indirect bundle Shade replays, and on Metal the push constants are baked
// into the ICB at prepare time -- the two halves cannot drift without corrupting the dispatch.
//
// Skipped entirely while a scene.gbuffer_debug view is selected: shading writes the same target the
// debug view was just rendered into, so leaving it on would overwrite what is being inspected.
class DeferredShadingPasses : public RenderPass
{
public:
    explicit DeferredShadingPasses(SceneRenderer& sceneRenderer) : m_SceneRenderer(&sceneRenderer) {}

    const char* Name() const override { return "Deferred Shading"; }

    void Register(RenderGraph& graph, FrameContext& ctx) override;

private:
    SceneRenderer* m_SceneRenderer;
};
