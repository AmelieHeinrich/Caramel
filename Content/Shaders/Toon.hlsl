/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Cel-shaded model: the same gbuffer as DefaultPBR, quantised into bands with a rim term. Exists
// mainly to prove the multi-shading-model path -- assign it to one entity in the inspector and only
// that entity's pixels take this dispatch.

#include "Common/DeferredShading.hlsli"

#pragma compute ToonCS

// One float4 (or int4) per parameter in Content/Materials/Schemes/Toon.json, in declaration order.
// Scalars live in .x and the rest of the slot is padding -- see MaterialScheme::ComputeLayout for
// why the layout is this blunt.
struct ToonParams {
    float4 vBrightness;
    int4   vBandCount;
    float4 vRimColor;
    float4 vRimPower;
};

[numthreads(kShadeGroupSize, 1, 1)]
void ToonCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel;
    uint materialSlot;
    if (!DeferredResolveThread(dispatchThreadID.x, pixel, materialSlot))
        return;

    DeferredSurface surface = DeferredLoadSurface(pixel, materialSlot);
    ToonParams params = DEFERRED_LOAD_SCHEME_PARAMS(ToonParams, materialSlot);

    float bands = (float)max(params.vBandCount.x, 1);

    // Banding needs one scalar, not a BRDF, so the same light list collapses into a weighted lambert
    // term. Radiance is folded down by luminance -- a cel ramp has no per-channel meaning.
    float NdotL = 0.0f;
    for (uint i = 0; i < g_Constants.uLightCount; ++i) {
        float3 lightDir;
        float3 radiance;
        if (!LightEvaluate(DeferredLoadLight(i), surface.vWorldPosition, lightDir, radiance))
            continue;

        NdotL += saturate(dot(surface.vNormal, lightDir)) * dot(radiance, float3(0.2126f, 0.7152f, 0.0722f));
    }
    NdotL = saturate(NdotL);

    // ceil rather than floor so the brightest band reaches 1.0 and the terminator lands on an actual
    // band edge instead of leaving the lit side capped at (bands-1)/bands.
    float banded = ceil(NdotL * bands) / bands;

    // Fresnel-ish rim, brightest where the surface turns away from the camera.
    float rim = pow(1.0f - saturate(dot(surface.vNormal, surface.vViewDirection)), params.vRimPower.x);

    float3 color = surface.vAlbedo * banded * params.vBrightness.x
                 + params.vRimColor.rgb * rim
                 + surface.vEmissive;

    // Linear and un-tonemapped: Composite.hlsl owns the transfer curve.
    DeferredWrite(pixel, color);
}
