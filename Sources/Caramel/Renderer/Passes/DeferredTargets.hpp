/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/FrameContext.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraph.hpp>

/// @brief The gbuffer-plus-output handle set every deferred shading path reads, resolved from the
/// frame's graph.
///
/// Must be called from inside a pass's execute callback, never at registration time: a transient's
/// bindless handle only exists once the allocator has given it a real texture, which is after
/// Compile(). Both DeferredShadingPasses' kernels and ReSTIRPass' stages therefore resolve it at
/// each call site rather than once per frame -- and both go through this one function so a shading
/// path cannot end up reading a different gbuffer than the one it declared.
inline DeferredTargets ResolveDeferredTargets(RGResolveContext& rc, const FrameContext& ctx)
{
    DeferredTargets targets{};
    targets.visibilityHandle = (uint32)rc.ResolveBindlessTexture(ctx.visibility);
    targets.depthHandle = ctx.hzbResources->depthHandle;
    targets.albedoHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[0]);
    targets.normalHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[1]);
    targets.metallicRoughnessHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[2]);
    targets.emissiveHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[3]);
    targets.motionHandle = (uint32)rc.ResolveBindlessTexture(ctx.gbuffer[4]);
    targets.sceneLightingUAVHandle = (uint32)rc.ResolveBindlessTexture(ctx.sceneLighting, true);
    return targets;
}
