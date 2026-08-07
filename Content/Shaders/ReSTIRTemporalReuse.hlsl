/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Stage 2 of ReSTIR DI: fold in the reservoir this surface held last frame, found by reprojecting
// the pixel through the motion gbuffer. This is where the effective sample count grows past the M
// candidates stage 1 could afford.
//
// !! SCAFFOLDING. The dispatch, the reprojection and the inputs are real; the combine is not written
// !! and there is no reservoir buffer to read or write. See Content/Shaders/Common/ReSTIR.hlsli.

#include "Common/ReSTIR.hlsli"

#pragma compute ReSTIRTemporalReuseCS

[numthreads(kReSTIRGroupSize, kReSTIRGroupSize, 1)]
void ReSTIRTemporalReuseCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    uint materialSlot;
    if (!ReSTIRResolvePixel(pixel, materialSlot))
        return;

    if (!ReSTIRHasReservoirs())
        return;

    DeferredSurface surface = ReSTIRLoadSurface(pixel, materialSlot);

    // Last frame's position is routinely off screen -- a disocclusion, or the camera turning -- and
    // that is not an error condition, it just means this pixel has no history to reuse.
    float2 prev = ReSTIRReproject(pixel);
    if (any(prev < 0.0f) || prev.x >= (float)g_Constants.uWidth || prev.y >= (float)g_Constants.uHeight)
        return;

    uint2 prevPixel = uint2(prev);

    // TODO: combine the history reservoir.
    //   reject prevPixel on a geometric mismatch (normal dot < ~0.9, relative depth > ~10%) --
    //     motion vectors reproject a *surface*, and a reprojected background or a different object
    //     is exactly how ReSTIR picks up ghosting
    //   load history from rReservoirsIn at ReSTIRReservoirOffset(prevPixel)
    //   clamp history.uM to ~20x this frame's M, or old samples never wash out and the image lags
    //   re-evaluate the history sample's target function against *this* pixel's surface -- it moved,
    //     so the stored p_hat is for the wrong shading point
    //   merge into this pixel's reservoir and store to rReservoirsOut
}
