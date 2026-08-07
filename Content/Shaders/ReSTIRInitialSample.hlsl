/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Stage 1 of ReSTIR DI: draw M candidate lights per pixel and keep one by resampled importance
// sampling, weighted by an unshadowed target function.
//
// !! SCAFFOLDING. The dispatch and the inputs are real; the RIS loop is not written and there is no
// !! reservoir buffer to write it into. See Content/Shaders/Common/ReSTIR.hlsli.

#include "Common/ReSTIR.hlsli"
#include "Common/BRDF.hlsli"

#pragma compute ReSTIRInitialSampleCS

[numthreads(kReSTIRGroupSize, kReSTIRGroupSize, 1)]
void ReSTIRInitialSampleCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    uint materialSlot;
    if (!ReSTIRResolvePixel(pixel, materialSlot))
        return;

    if (!ReSTIRHasReservoirs())
        return;

    DeferredSurface surface = ReSTIRLoadSurface(pixel, materialSlot);

    // Exactly the set the reference path evaluates -- the pixel's cluster plus the directional
    // lights, or every light when scene.clustered_lights is off. Sampling anything else makes the
    // estimator converge to an image ReSTIRShade's brute-force loop never produces.
    DeferredLightList lights = ReSTIRBeginLights(surface.vWorldPosition, pixel);
    uint lightCount = DeferredLightListTotal(lights);
    if (lightCount == 0)
        return;

    uint rng = ReSTIRSeed(pixel);

    // TODO: RIS over M candidates.
    //   for each of M candidates:
    //     pick j uniformly in [0, lightCount), source pdf = 1 / lightCount
    //     light = ReSTIRLoadLight(ReSTIRLightIndex(lights, j))
    //     LightEvaluate -> target function p_hat = |CookTorrance(...) * radiance| (unshadowed)
    //     w = p_hat / sourcePdf; reservoir.Update(lightIndex, w, ReSTIRRandom(rng))
    //   reservoir.fW = reservoir.fWeightSum / (reservoir.uM * p_hat(chosen))
    //   trace one shadow ray against the TLAS for the survivor and zero fW if occluded
    //     (AccelerationStructureManager builds it -- render.build_acceleration_structures)
    //   store into rReservoirsOut at ReSTIRReservoirOffset(pixel)
}
