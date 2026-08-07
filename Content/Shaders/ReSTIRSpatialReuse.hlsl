/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Stage 3 of ReSTIR DI: merge a few neighbouring reservoirs, so a pixel that sampled badly can
// borrow from one that did. Cheap where temporal reuse is not: no history, no reprojection.
//
// !! SCAFFOLDING. The dispatch and the inputs are real; the merge is not written and there is no
// !! reservoir buffer to read or write. See Content/Shaders/Common/ReSTIR.hlsli.

#include "Common/ReSTIR.hlsli"

#pragma compute ReSTIRSpatialReuseCS

// Neighbours sampled per pixel, and the radius they are drawn from. Both are the usual starting
// point; they trade variance against how far light leaks across a geometric edge the similarity
// test fails to catch.
static const uint  kSpatialSampleCount = 5;
static const float kSpatialRadius = 30.0f;

[numthreads(kReSTIRGroupSize, kReSTIRGroupSize, 1)]
void ReSTIRSpatialReuseCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    uint materialSlot;
    if (!ReSTIRResolvePixel(pixel, materialSlot))
        return;

    if (!ReSTIRHasReservoirs())
        return;

    DeferredSurface surface = ReSTIRLoadSurface(pixel, materialSlot);
    uint rng = ReSTIRSeed(pixel);

    // TODO: merge kSpatialSampleCount neighbours.
    //   for each: offset = kSpatialRadius * uniform disk sample from ReSTIRRandom(rng), clamped to
    //     the viewport, skipped if ReSTIRResolvePixel rejects it
    //   reject on the same geometric similarity test temporal reuse uses -- normal and depth
    //   re-evaluate the neighbour's sample against this surface before merging, same reason
    //   apply the MIS weight (Talbot / pairwise) rather than a flat 1/M, or shared samples get
    //     counted repeatedly and the image brightens where reuse is densest
    //
    // NOTE: this stage reads and writes the same reservoir set. Either ping-pong a third buffer or
    // read-then-write with the neighbourhood snapshotted -- reading a neighbour another thread has
    // already overwritten is a correlation bug that looks like blotching, not like a race.
}
