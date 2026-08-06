/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "CompositePass.hpp"

#include <Caramel/Renderer/SceneRenderer.hpp>

void CompositePass::Register(RenderGraph& graph, FrameContext& ctx)
{
    graph.AddAttachmentPass("Composite",
        [&](RGPassBuilder& builder) {
            RGAttachmentDesc sceneColorAttachment{};
            sceneColorAttachment.texture = ctx.sceneColor;
            sceneColorAttachment.loadOp = agfx::LoadOp::Clear;
            sceneColorAttachment.storeOp = agfx::StoreOp::Store;
            sceneColorAttachment.clearColor[3] = 1.0f;
            builder.AddColorAttachment(sceneColorAttachment);

            builder.ReadTexture(ctx.sceneLighting, agfx::ResourceState::PixelShaderResource);
        },
        [this, &ctx](agfx::RenderPass& pass, RGResolveContext& rc) {
            uint32 sceneLightingHandle = (uint32)rc.ResolveBindlessTexture(ctx.sceneLighting);
            m_SceneRenderer->RenderComposite(pass, sceneLightingHandle, ctx.width, ctx.height);
        });
}
