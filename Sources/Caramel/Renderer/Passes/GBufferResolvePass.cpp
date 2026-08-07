/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "GBufferResolvePass.hpp"

#include <Caramel/Renderer/SceneRenderer.hpp>

void GBufferResolvePass::Register(RenderGraph& graph, FrameContext& ctx)
{
    graph.AddAttachmentPass("GBuffer Resolve",
        [&](RGPassBuilder& builder) {
            // Attachment order is the contract with the GBuffer Resolve pipeline (SceneRenderer) and
            // GBufferOut in GBufferResolve.hlsl: scene lighting first, then the gbuffer.
            RGAttachmentDesc sceneLightingAttachment{};
            sceneLightingAttachment.texture = ctx.sceneLighting;
            sceneLightingAttachment.loadOp = agfx::LoadOp::Clear;
            sceneLightingAttachment.storeOp = agfx::StoreOp::Store;
            sceneLightingAttachment.clearColor[3] = 1.0f;
            builder.AddColorAttachment(sceneLightingAttachment);

            for (uint32 i = 0; i < kGBufferTextureCount; ++i) {
                RGAttachmentDesc attachment{};
                attachment.texture = ctx.gbuffer[i];
                attachment.loadOp = agfx::LoadOp::Clear;
                attachment.storeOp = agfx::StoreOp::Store;
                builder.AddColorAttachment(attachment);
            }

            builder.ReadTexture(ctx.visibility, agfx::ResourceState::PixelShaderResource);
            builder.ReadTexture(ctx.depth, agfx::ResourceState::PixelShaderResource);
        },
        [this, &ctx](agfx::RenderPass& pass, RGResolveContext& rc) {
            uint32 visibilityHandle = (uint32)rc.ResolveBindlessTexture(ctx.visibility);
            m_SceneRenderer->RenderGBufferResolve(pass, *ctx.gpuScene, visibilityHandle,
                                                  ctx.hzbResources->depthHandle, ctx.clusters,
                                                  ctx.width, ctx.height, ctx.frameIndex);
        });
}
