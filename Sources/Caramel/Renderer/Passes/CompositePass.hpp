/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

class SceneRenderer;

// Fullscreen resolve of the HDR lighting buffer into scene color. The smallest pass in the renderer,
// and the one to copy when adding a new fullscreen technique.
class CompositePass : public RenderPass
{
public:
    explicit CompositePass(SceneRenderer& sceneRenderer) : m_SceneRenderer(&sceneRenderer) {}

    const char* Name() const override { return "Composite"; }

    void Register(RenderGraph& graph, FrameContext& ctx) override;

private:
    SceneRenderer* m_SceneRenderer;
};
