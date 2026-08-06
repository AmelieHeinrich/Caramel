/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 15:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Step 3 of 3: write every covered pixel into its scheme's contiguous run of the pixel list. Must
// bin pixels exactly the way MaterialClassifyCount did -- that is why both go through
// DeferredClassifyPixel rather than each rolling their own test. A disagreement would let one
// scheme's run overflow into the next.

#include "Common/DeferredShading.hlsli"

#pragma compute MaterialClassifyScatterCS

groupshared uint g_TileCounts[kMaxShadingSchemes];
groupshared uint g_TileBases[kMaxShadingSchemes];

[numthreads(kClassifyTileSize, kClassifyTileSize, 1)]
void MaterialClassifyScatterCS(uint3 dispatchThreadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex < kMaxShadingSchemes)
        g_TileCounts[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    uint schemeId, materialSlot;
    bool covered = DeferredClassifyPixel(dispatchThreadID.xy, schemeId, materialSlot);

    // Two-level allocation: the tile claims one contiguous run per scheme with a single global
    // atomic, then each thread takes its slot inside that run from groupshared. Same reason as the
    // count pass -- a global atomic per pixel is almost entirely contention.
    uint localIndex = 0;
    if (covered)
        InterlockedAdd(g_TileCounts[schemeId], 1, localIndex);
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex < g_Constants.uSchemeCount)
    {
        uint base = 0;
        if (g_TileCounts[groupIndex] > 0)
        {
            AGFXRWByteAddressBuffer bClassify = AGFXRWByteAddressBuffer::Create(g_Constants.rClassifyBuffer);
            bClassify.InterlockedAdd(kClassifyCursorBase + groupIndex * 4, g_TileCounts[groupIndex], base);
        }
        g_TileBases[groupIndex] = base;
    }
    GroupMemoryBarrierWithGroupSync();

    // The cursor was seeded with the run's own offset, so this index is already absolute.
    if (covered)
    {
        AGFXRWByteAddressBuffer bPixelList = AGFXRWByteAddressBuffer::Create(g_Constants.rPixelList);
        bPixelList.Store((g_TileBases[schemeId] + localIndex) * 4, DeferredPackPixel(dispatchThreadID.xy));
    }
}
