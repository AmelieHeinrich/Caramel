/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Stage 4 of ReSTIR DI: turn the surviving reservoir into radiance and write the lighting buffer.
//
// The body below is deliberately NOT ReSTIR yet -- it is the same brute-force loop DefaultPBR's
// kernel runs, over the same light list. That makes toggling scene.restir on a no-op on the image
// (modulo material schemes, which this path bypasses), which is exactly what the reuse stages need
// to be diffed against as they land: a wrong reservoir shows up as a difference from this, and there
// is nothing to compare against if the baseline is a black screen.
//
// Standalone by design: no material classification, no per-scheme dispatch, so a Toon material
// shades as PBR while this path is on.

#include "Common/ReSTIR.hlsli"
#include "Common/BRDF.hlsli"

#pragma compute ReSTIRShadeCS

[numthreads(kReSTIRGroupSize, kReSTIRGroupSize, 1)]
void ReSTIRShadeCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    uint materialSlot;
    if (!ReSTIRResolvePixel(pixel, materialSlot))
        return;

    DeferredSurface surface = ReSTIRLoadSurface(pixel, materialSlot);

    // TODO: replace this loop with the single weighted sample once reservoirs exist:
    //   reservoir = load from rReservoirsOut at ReSTIRReservoirOffset(pixel)
    //   if (reservoir.uSampleIndex == ~0u || reservoir.fW <= 0) direct = 0
    //   else LightEvaluate(that one light) -> CookTorrance(...) * radiance * reservoir.fW
    // The whole point of ReSTIR is that this becomes O(1) in the light count.
    DeferredLightList lights = ReSTIRBeginLights(surface.vWorldPosition, pixel);

    float3 direct = 0.0f;
    for (uint i = 0; i < DeferredLightListTotal(lights); ++i) {
        float3 lightDir;
        float3 radiance;
        if (!LightEvaluate(ReSTIRLoadLight(ReSTIRLightIndex(lights, i)), surface.vWorldPosition, lightDir, radiance))
            continue;

        direct += CookTorrance(surface.vNormal, surface.vViewDirection, lightDir,
                               surface.vAlbedo, surface.fMetallic, surface.fRoughness) * radiance;
    }

    // scene.restir_ambient rather than a per-material scheme parameter: this path has no scheme to
    // read one from. Same job as DefaultPBR's ambientScale -- keeping an unlit scene from pure black.
    float3 ambient = surface.vAlbedo * g_Constants.fAmbient;
    float3 color = direct + ambient + surface.vEmissive;

    // Linear and un-tonemapped: Composite.hlsl owns the transfer curve.
    ReSTIRWrite(pixel, color);
}
