/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 15:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Step 1 of 3: how many pixels does each material scheme own this frame. See DeferredShading.hlsli
// for the buffer layout and the shared classification rule.

#include "Common/DeferredShading.hlsli"

#pragma compute MaterialClassifyCountCS

groupshared uint g_TileCounts[kMaxShadingSchemes];

[numthreads(kClassifyTileSize, kClassifyTileSize, 1)]
void MaterialClassifyCountCS(uint3 dispatchThreadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex < kMaxShadingSchemes)
        g_TileCounts[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    uint schemeId, materialSlot;
    if (DeferredClassifyPixel(dispatchThreadID.xy, schemeId, materialSlot))
        InterlockedAdd(g_TileCounts[schemeId], 1);

    GroupMemoryBarrierWithGroupSync();

    // One global atomic per (tile, scheme) rather than one per pixel. With a handful of schemes in a
    // scene, a per-pixel atomic on a global counter is nearly pure contention.
    if (groupIndex < g_Constants.uSchemeCount && g_TileCounts[groupIndex] > 0)
    {
        AGFXRWByteAddressBuffer bClassify = AGFXRWByteAddressBuffer::Create(g_Constants.rClassifyBuffer);
        uint previous;
        bClassify.InterlockedAdd(kClassifyCountBase + groupIndex * 4, g_TileCounts[groupIndex], previous);
    }
}
