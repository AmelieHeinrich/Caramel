/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "DeferredShadingPasses.hpp"

#include <Caramel/Renderer/SceneRenderer.hpp>
#include <Caramel/Renderer/Passes/DeferredTargets.hpp>
#include <Caramel/Renderer/Passes/ReSTIRPass.hpp>

// ReSTIR writes the same target these passes do, so exactly one of the two paths runs per frame.
// The CVar lives in ReSTIRPass because that is the pass it switches on.
bool DeferredShadingPasses::Enabled(const FrameContext& ctx) const
{
    return !ReSTIRPass::IsShadingActive();
}

void DeferredShadingPasses::Register(RenderGraph& graph, FrameContext& ctx)
{
    graph.AddPass("Material Classify",
        [&](RGPassBuilder& builder) {
            builder.ReadTexture(ctx.visibility, agfx::ResourceState::NonPixelShaderResource);
            builder.ReadTexture(ctx.depth, agfx::ResourceState::NonPixelShaderResource);
            // Its real outputs -- the classify buffer, the pixel list and the deferred bundle -- are
            // raw agfx objects with their own manual barrier sequence, invisible to the graph.
            builder.AlwaysExecute();
        },
        [this, &ctx](agfx::CommandBuffer& cmd, RGResolveContext& rc) {
            m_SceneRenderer->ClassifyMaterials(cmd, *ctx.gpuScene, ResolveDeferredTargets(rc, ctx), ctx.clusters,
                                               ctx.width, ctx.height, ctx.frameIndex);
        });

    graph.AddPass("Material Shade",
        [&](RGPassBuilder& builder) {
            for (uint32 i = 0; i < kGBufferTextureCount; ++i)
                builder.ReadTexture(ctx.gbuffer[i], agfx::ResourceState::NonPixelShaderResource);
            builder.ReadTexture(ctx.visibility, agfx::ResourceState::NonPixelShaderResource);
            builder.ReadTexture(ctx.depth, agfx::ResourceState::NonPixelShaderResource);
            builder.WriteTexture(ctx.sceneLighting, agfx::ResourceState::UnorderedAccess);
        },
        [this, &ctx](agfx::CommandBuffer& cmd, RGResolveContext& rc) {
            m_SceneRenderer->ShadeMaterials(cmd, *ctx.gpuScene, ResolveDeferredTargets(rc, ctx), ctx.clusters,
                                            ctx.width, ctx.height, ctx.frameIndex);
        });
}
