/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 15:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Step 2 of 3: turn the per-scheme counts into pixel-list offsets, and write one indirect dispatch
// command per scheme. A single group of kMaxShadingSchemes threads -- the whole working set is a
// handful of uints, so a real parallel scan would cost more than it saves.

#include "Common/DeferredShading.hlsli"

#pragma compute MaterialClassifyArgsCS

[numthreads(1, 1, 1)]
void MaterialClassifyArgsCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    AGFXRWByteAddressBuffer bClassify = AGFXRWByteAddressBuffer::Create(g_Constants.rClassifyBuffer);
    AGFXIndirectDispatchBundle bundle = AGFXIndirectDispatchBundle::Create(DeferredBundleHandle());

    uint offset = 0;
    for (uint scheme = 0; scheme < g_Constants.uSchemeCount; ++scheme)
    {
        uint count = bClassify.Load(kClassifyCountBase + scheme * 4);

        bClassify.Store(kClassifyOffsetBase + scheme * 4, offset);
        // The scatter pass allocates within the run by bumping this from the run's own base, so the
        // cursor starts where the run does rather than at zero.
        bClassify.Store(kClassifyCursorBase + scheme * 4, offset);
        offset += count;

        // A scheme with no pixels appends nothing, so its count slot stays 0 and its
        // ExecuteIndirectBundle replays zero dispatches. Command slot and count slot are both the
        // scheme id -- one command per region (see .claude/agfx-mdi/SKILL.md gotcha 4).
        if (count > 0)
            bundle.Dispatch(scheme, scheme, (count + kShadeGroupSize - 1) / kShadeGroupSize, 1, 1);
    }
}
