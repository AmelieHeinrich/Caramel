/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:19:23
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <AGFX/agfx.hpp>

#define FRAMES_IN_FLIGHT 3

// HDR shading buffer sitting between the gbuffer and scene color. Shared here because three places
// have to agree on it: the texture Renderer creates, the GBuffer Resolve pipeline's attachment 0,
// and the Composite pipeline that reads it. RGBA16F rather than the swap chain format because the
// scheme dispatches write it from compute, and a typed UAV on BGRA8Unorm is optional in D3D12.
constexpr agfx::TextureFormat kSceneLightingFormat = agfx::TextureFormat::RGBA16F;

// The single-pass downsampler writes mips 0..12 in one dispatch, which covers an HZB up to 4096 on
// its longest side. Nothing deeper is reachable without a second dispatch.
constexpr uint32 kMaxHZBMips = 13;

// Everything SceneRenderer needs to build and sample the depth pyramid. Owned by Renderer (the HZB
// outlives any single frame's render graph, and a mip chain needs one writeable view per mip, which
// RGResolveContext::ResolveBindlessTexture cannot produce), handed down per pass.
// Bindless handles the deferred material passes read and write. Same ownership split as
// HZBResources: Renderer owns the textures (they outlive any one frame's render graph, and the
// lighting buffer needs a writeable view RGResolveContext::ResolveBindlessTexture cannot produce),
// SceneRenderer just consumes the handles. Gbuffer order matches Renderer::m_GBufferTextures and
// GBufferOut in GBufferResolve.hlsl.
struct DeferredTargets
{
    uint32 visibilityHandle;
    uint32 depthHandle;
    uint32 albedoHandle;
    uint32 normalHandle;
    uint32 metallicRoughnessHandle;
    uint32 emissiveHandle;
    uint32 sceneLightingUAVHandle;   // writeable view -- what the scheme dispatches store into
};

struct HZBResources
{
    uint32 depthHandle;              // read-only SRV over the depth buffer, source of the pyramid
    uint32 depthWidth;
    uint32 depthHeight;
    uint32 hzbHandle;                // read-only view over the whole chain, what culling samples
    uint32 mipHandles[kMaxHZBMips];  // writeable view per mip, what the downsampler writes
    uint32 width;                    // mip 0 dimensions -- the depth buffer's, rounded down to a power of two
    uint32 height;
    uint32 mipCount;
};
