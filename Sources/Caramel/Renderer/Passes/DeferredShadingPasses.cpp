/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "DeferredShadingPasses.hpp"

#include <Caramel/Renderer/SceneRenderer.hpp>

namespace
{
    // Both kernels take the same target set, so it is resolved identically at both call sites rather
    // than built once outside the graph -- a transient's bindless handle only exists once the
    // allocator has given it a real texture, which is after Compile(), not at registration time.
    DeferredTargets ResolveTargets(RGResolveContext& rc, const FrameContext& ctx)
    {
        DeferredTargets targets{};
        targets.visibilityHandle = (uint32)rc.ResolveBindlessTexture(ctx.visibility);
        targets.depthHandle = ctx.hzbResources->depthHandle;
        targets.albedoHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[0]);
        targets.normalHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[1]);
        targets.metallicRoughnessHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[2]);
        targets.emissiveHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[3]);
        targets.sceneLightingUAVHandle = (uint32)rc.ResolveBindlessTexture(ctx.sceneLighting, true);
        return targets;
    }
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
            m_SceneRenderer->ClassifyMaterials(cmd, *ctx.gpuScene, ResolveTargets(rc, ctx),
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
            m_SceneRenderer->ShadeMaterials(cmd, *ctx.gpuScene, ResolveTargets(rc, ctx),
                                            ctx.width, ctx.height, ctx.frameIndex);
        });
}
