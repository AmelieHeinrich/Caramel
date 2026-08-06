/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

class SceneRenderer;

// Fullscreen visibility-buffer resolve: reads the visibility + depth targets and writes the whole
// gbuffer. Attachment 0 is the scene lighting buffer, which this pass only clears (or fills with the
// scene.gbuffer_debug view) -- DeferredShadingPasses is what actually shades it.
class GBufferResolvePass : public RenderPass
{
public:
    explicit GBufferResolvePass(SceneRenderer& sceneRenderer) : m_SceneRenderer(&sceneRenderer) {}

    const char* Name() const override { return "GBuffer Resolve"; }

    void Register(RenderGraph& graph, FrameContext& ctx) override;

private:
    SceneRenderer* m_SceneRenderer;
};
