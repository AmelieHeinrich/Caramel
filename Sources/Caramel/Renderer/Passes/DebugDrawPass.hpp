/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

class DebugRenderer;

// Flushes the frame's queued debug geometry over scene color. Opens its own render passes internally,
// so it registers as a generic pass and resolves its own render targets.
class DebugDrawPass : public RenderPass
{
public:
    explicit DebugDrawPass(DebugRenderer& debugRenderer) : m_DebugRenderer(&debugRenderer) {}

    const char* Name() const override { return "Debug Draw"; }

    void Register(RenderGraph& graph, FrameContext& ctx) override;

private:
    DebugRenderer* m_DebugRenderer;
};
