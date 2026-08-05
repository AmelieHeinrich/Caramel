/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:19:23
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#define FRAMES_IN_FLIGHT 3

// The single-pass downsampler writes mips 0..12 in one dispatch, which covers an HZB up to 4096 on
// its longest side. Nothing deeper is reachable without a second dispatch.
constexpr uint32 kMaxHZBMips = 13;

// Everything SceneRenderer needs to build and sample the depth pyramid. Owned by Renderer (the HZB
// outlives any single frame's render graph, and a mip chain needs one writeable view per mip, which
// RGResolveContext::ResolveBindlessTexture cannot produce), handed down per pass.
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
