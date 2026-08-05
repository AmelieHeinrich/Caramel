/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Single-pass downsampler that turns the depth buffer into a max-reduction depth pyramid.
// Structured like FidelityFX SPD: one 256-thread group owns a 64x64 tile of mip 0 and reduces it
// all the way down to that tile's single mip-6 texel; the last group to finish (decided by a global
// atomic) picks mip 6 back up and finishes mips 7..12. The whole chain lands in one dispatch.
//
// The reduction is max, not min: depth is non-reversed-Z (Less, cleared to 1.0), so a box is hidden
// only when its nearest point is behind the *farthest* surface covering its screen rect. Pixels
// nothing was drawn into keep the 1.0 clear, which is exactly "never occludes".

#include "Common/AGFX.hlsli"

#pragma compute BuildHZBCS

struct BuildHZBPushConstants {
    ResourceHandle rSrcDepth;
    ResourceHandle rCounter;    // single uint, zeroed by the host every frame
    uint4          rHZBMips[4]; // 16 UAV slots, one per mip. uint4[4] rather than uint[13] because
                                // constant-buffer arrays pad each element to 16 bytes -- a uint[13]
                                // would cost 208 bytes and blow the 128-byte push-constant budget.
    uint2          uSrcDepthSize;
    uint2          uHZBSize;    // mip 0 dimensions, both powers of two
    uint           uMipCount;
    uint           uNumWorkGroups;
};
AGFX_PUSH_CONSTANTS(BuildHZBPushConstants, g_Constants);

static const uint kHZBTileSize = 64;
// Mip 6 is where a group hands its result to whichever group finishes last, so it is the one level
// that has to be written and read through a globally coherent view rather than the normal one.
static const uint kHZBHandoffMip = 6;

groupshared float s_Tile[16][16];
groupshared uint  s_GroupsDone;

uint HZBMipHandle(uint mip) { return g_Constants.rHZBMips[mip >> 2][mip & 3]; }

uint2 HZBMipSize(uint mip) {
    return uint2(max(g_Constants.uHZBSize.x >> mip, 1u), max(g_Constants.uHZBSize.y >> mip, 1u));
}

void HZBStore(uint mip, uint2 p, float value) {
    if (mip >= g_Constants.uMipCount)
        return;
    uint2 size = HZBMipSize(mip);
    if (p.x >= size.x || p.y >= size.y)
        return;

    if (mip == kHZBHandoffMip) {
        globallycoherent RWTexture2D<float> handoff = ResourceDescriptorHeap[HZBMipHandle(mip)];
        handoff[p] = value;
    } else {
        AGFXRWTexture2D<float>::Create(HZBMipHandle(mip)).Store(int2(p), value);
    }
}

float HZBLoadHandoff(uint2 p) {
    // Clamping duplicates an edge texel rather than reading past the mip. That is safe for a max
    // reduction: the duplicate is a value that genuinely belongs to the parent's footprint.
    p = min(p, HZBMipSize(kHZBHandoffMip) - 1);
    globallycoherent RWTexture2D<float> handoff = ResourceDescriptorHeap[HZBMipHandle(kHZBHandoffMip)];
    return handoff[p];
}

// Mip 0 is the depth buffer rounded *down* to a power of two, so one mip-0 texel covers between one
// and two depth texels -- and once the fractional offset lands badly, a footprint of e.g. 1.875
// straddles three of them (1920 -> 1024 does exactly this on the x axis). Sampling the exact covered
// range is what keeps the result an over-estimate of the true max; missing one farther texel would
// make the pyramid claim geometry is behind an occluder that does not actually cover it, and cull
// visible geometry away.
float HZBReduceDepth(uint2 p) {
    Texture2D<float> depth = ResourceDescriptorHeap[g_Constants.rSrcDepth];

    uint2 maxCoord = g_Constants.uSrcDepthSize - 1;
    uint2 lo = min(p * g_Constants.uSrcDepthSize / g_Constants.uHZBSize, maxCoord);
    uint2 hi = min(((p + 1) * g_Constants.uSrcDepthSize - 1) / g_Constants.uHZBSize, maxCoord);

    float result = 0.0f;
    for (uint y = lo.y; y <= hi.y; ++y) {
        for (uint x = lo.x; x <= hi.x; ++x)
            result = max(result, depth.Load(int3(x, y, 0)));
    }
    return result;
}

float Max4(float a, float b, float c, float d) { return max(max(a, b), max(c, d)); }

// Reduces the 16x16 block sitting in s_Tile (which belongs to mip firstMip-1) down to a single
// value, writing mips firstMip .. firstMip+3 along the way. tileOrigin is in mip-0 texels.
void HZBReduceGroupshared(uint2 t, uint2 tileOrigin, uint firstMip) {
    [unroll]
    for (uint step = 0; step < 4; ++step) {
        GroupMemoryBarrierWithGroupSync();

        uint dim = 8u >> step;
        bool active = t.x < dim && t.y < dim;

        float value = 0.0f;
        if (active) {
            value = Max4(s_Tile[t.y * 2][t.x * 2],     s_Tile[t.y * 2][t.x * 2 + 1],
                         s_Tile[t.y * 2 + 1][t.x * 2], s_Tile[t.y * 2 + 1][t.x * 2 + 1]);
            HZBStore(firstMip + step, (tileOrigin >> (firstMip + step)) + t, value);
        }

        // The write below lands on cells other threads are still reading above.
        GroupMemoryBarrierWithGroupSync();
        if (active)
            s_Tile[t.y][t.x] = value;
    }
}

[numthreads(256, 1, 1)]
void BuildHZBCS(uint3 uGroupID : SV_GroupID, uint uGroupIndex : SV_GroupIndex) {
    uint2 t = uint2(uGroupIndex & 15u, uGroupIndex >> 4u);
    uint2 tileOrigin = uGroupID.xy * kHZBTileSize;

    // Mip 0: 64x64 per tile, so 4x4 texels per thread.
    float block[16];
    [unroll]
    for (uint dy = 0; dy < 4; ++dy) {
        [unroll]
        for (uint dx = 0; dx < 4; ++dx) {
            uint2 p = tileOrigin + t * 4 + uint2(dx, dy);
            float value = HZBReduceDepth(p);
            block[dy * 4 + dx] = value;
            HZBStore(0, p, value);
        }
    }

    // Mip 1: 32x32 per tile, 2x2 per thread, entirely out of the registers above.
    float quad[4];
    [unroll]
    for (uint qy = 0; qy < 2; ++qy) {
        [unroll]
        for (uint qx = 0; qx < 2; ++qx) {
            float value = Max4(block[(qy * 2) * 4 + qx * 2],       block[(qy * 2) * 4 + qx * 2 + 1],
                               block[(qy * 2 + 1) * 4 + qx * 2],   block[(qy * 2 + 1) * 4 + qx * 2 + 1]);
            quad[qy * 2 + qx] = value;
            HZBStore(1, (tileOrigin >> 1) + t * 2 + uint2(qx, qy), value);
        }
    }

    // Mip 2: 16x16 per tile, one per thread -- from here down the reduction has to go through LDS.
    float mip2 = Max4(quad[0], quad[1], quad[2], quad[3]);
    HZBStore(2, (tileOrigin >> 2) + t, mip2);
    s_Tile[t.y][t.x] = mip2;

    HZBReduceGroupshared(t, tileOrigin, 3);

    // A pyramid this shallow is finished: mip 6 was the last level, and with a tile grid of one
    // group there is nothing to hand off anyway.
    if (g_Constants.uMipCount <= kHZBHandoffMip + 1)
        return;

    // Publish this group's mip-6 texel before claiming a ticket, so whoever draws the last ticket is
    // guaranteed to see every other group's contribution.
    DeviceMemoryBarrierWithGroupSync();
    if (uGroupIndex == 0) {
        globallycoherent RWByteAddressBuffer counter = ResourceDescriptorHeap[g_Constants.rCounter];
        uint previous;
        counter.InterlockedAdd(0, 1, previous);
        s_GroupsDone = previous;
    }
    GroupMemoryBarrierWithGroupSync();
    if (s_GroupsDone != g_Constants.uNumWorkGroups - 1)
        return;

    // Second phase, last group only: mip 6 is at most 64x64 (mip 0 is at most 4096 wide), so the
    // same 16x16 threads x 4x4 texels shape covers it in one go.
    [unroll]
    for (uint hy = 0; hy < 4; ++hy) {
        [unroll]
        for (uint hx = 0; hx < 4; ++hx)
            block[hy * 4 + hx] = HZBLoadHandoff(t * 4 + uint2(hx, hy));
    }

    [unroll]
    for (uint ry = 0; ry < 2; ++ry) {
        [unroll]
        for (uint rx = 0; rx < 2; ++rx) {
            float value = Max4(block[(ry * 2) * 4 + rx * 2],     block[(ry * 2) * 4 + rx * 2 + 1],
                               block[(ry * 2 + 1) * 4 + rx * 2], block[(ry * 2 + 1) * 4 + rx * 2 + 1]);
            quad[ry * 2 + rx] = value;
            HZBStore(kHZBHandoffMip + 1, t * 2 + uint2(rx, ry), value);
        }
    }

    float mip8 = Max4(quad[0], quad[1], quad[2], quad[3]);
    HZBStore(kHZBHandoffMip + 2, t, mip8);
    s_Tile[t.y][t.x] = mip8;

    // Tile origin is zero from here on -- the last group covers the whole of what is left.
    HZBReduceGroupshared(t, uint2(0, 0), kHZBHandoffMip + 3);
}
