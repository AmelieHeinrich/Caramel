/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "DebugDrawPass.hpp"

#include <Caramel/Renderer/DebugRenderer.hpp>

void DebugDrawPass::Register(RenderGraph& graph, FrameContext& ctx)
{
    graph.AddPass("Debug Draw",
        [&](RGPassBuilder& builder) {
            builder.WriteTexture(ctx.sceneColor, agfx::ResourceState::RenderTarget);
            builder.WriteTexture(ctx.depth, agfx::ResourceState::DepthWrite);
            // Scene color is sampled by ImGui next frame through a raw bindless handle
            // (GetViewportTextureID()), entirely outside the graph.
            builder.MarkAsExternallyRead(ctx.sceneColor, agfx::ResourceState::PixelShaderResource);
        },
        [this, &ctx](agfx::CommandBuffer& cmd, RGResolveContext& rc) {
            agfx::RenderTarget& colorTarget = rc.ResolveRenderTarget(ctx.sceneColor, false);
            agfx::RenderTarget& depthTarget = rc.ResolveRenderTarget(ctx.depth, true);
            m_DebugRenderer->Flush(cmd, colorTarget, depthTarget, *ctx.camera, ctx.width, ctx.height, ctx.frameIndex);
        });
}
