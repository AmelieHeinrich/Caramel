/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "VisibilityPasses.hpp"

#include <Caramel/Renderer/SceneRenderer.hpp>

void VisibilityPasses::BeginFrame(FrameContext& ctx)
{
    m_SceneRenderer->BeginFrame(*ctx.camera, ctx.width, ctx.height, ctx.frameIndex);
}

void VisibilityPasses::Register(RenderGraph& graph, FrameContext& ctx)
{
    const HZBResources& hzb = *ctx.hzbResources;

    graph.AddPass("Cull Early",
        [&](RGPassBuilder& builder) {
            // Touches no RG-tracked resource (the bundles are raw agfx objects with their own manual
            // barrier sequence), so it would otherwise be culled.
            builder.AlwaysExecute();
        },
        [this, &ctx, &hzb](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_SceneRenderer->CullEarly(cmd, *ctx.gpuScene, hzb, ctx.frameIndex);
        });

    graph.AddAttachmentPass("Scene Early",
        [&](RGPassBuilder& builder) {
            // Zero is the only portable clear value for a uint attachment (Vulkan reinterprets the
            // float bits) -- background pixels are detected via depth == 1.0 in the resolve, never
            // via a uint sentinel.
            RGAttachmentDesc colorAttachment{};
            colorAttachment.texture = ctx.visibility;
            colorAttachment.loadOp = agfx::LoadOp::Clear;
            colorAttachment.storeOp = agfx::StoreOp::Store;
            builder.AddColorAttachment(colorAttachment);

            RGAttachmentDesc depthAttachment{};
            depthAttachment.texture = ctx.depth;
            depthAttachment.loadOp = agfx::LoadOp::Clear;
            depthAttachment.storeOp = agfx::StoreOp::Store;
            depthAttachment.clearDepth = 1.0f;
            builder.SetDepthAttachment(depthAttachment);

            // SceneAS occlusion-culls meshlets against whatever the pyramid holds at this point,
            // which is the one last frame left behind.
            builder.ReadTexture(ctx.hzb, agfx::ResourceState::NonPixelShaderResource);
        },
        [this, &ctx, &hzb](agfx::RenderPass& pass, RGResolveContext&) {
            m_SceneRenderer->RenderEarly(pass, *ctx.gpuScene, hzb, ctx.width, ctx.height, ctx.frameIndex);
        });

    graph.AddPass("Build HZB",
        [&](RGPassBuilder& builder) {
            builder.ReadTexture(ctx.depth, agfx::ResourceState::NonPixelShaderResource);
            builder.WriteTexture(ctx.hzb, agfx::ResourceState::UnorderedAccess);
            // The pyramid outlives the graph: next frame's early pass samples it before anything
            // rebuilds it, so it has to be left readable, not in UnorderedAccess.
            builder.MarkAsExternallyRead(ctx.hzb, agfx::ResourceState::NonPixelShaderResource);
        },
        [this, &hzb](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_SceneRenderer->BuildHZB(cmd, hzb);
        });

    graph.AddPass("Cull Late",
        [&](RGPassBuilder& builder) {
            builder.ReadTexture(ctx.hzb, agfx::ResourceState::NonPixelShaderResource);
            builder.AlwaysExecute();
        },
        [this, &ctx, &hzb](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_SceneRenderer->CullLate(cmd, *ctx.gpuScene, hzb, ctx.frameIndex);
        });

    graph.AddAttachmentPass("Scene Late",
        [&](RGPassBuilder& builder) {
            RGAttachmentDesc colorAttachment{};
            colorAttachment.texture = ctx.visibility;
            colorAttachment.loadOp = agfx::LoadOp::Load;
            colorAttachment.storeOp = agfx::StoreOp::Store;
            builder.AddColorAttachment(colorAttachment);

            RGAttachmentDesc depthAttachment{};
            depthAttachment.texture = ctx.depth;
            depthAttachment.loadOp = agfx::LoadOp::Load;
            depthAttachment.storeOp = agfx::StoreOp::Store;
            builder.SetDepthAttachment(depthAttachment);

            builder.ReadTexture(ctx.hzb, agfx::ResourceState::NonPixelShaderResource);
        },
        [this, &ctx, &hzb](agfx::RenderPass& pass, RGResolveContext&) {
            m_SceneRenderer->RenderLate(pass, *ctx.gpuScene, hzb, ctx.width, ctx.height, ctx.frameIndex);
        });
}
