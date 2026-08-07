/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:40:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Standard metallic/roughness shading model, run as a deferred compute pass. Replayed from the
// Dispatch indirect bundle the material classification pass fills, so every thread here is already
// known to sit on a pixel whose material uses this scheme -- there is no per-pixel scheme branch.

#include "Common/DeferredShading.hlsli"
#include "Common/BRDF.hlsli"

#pragma compute DefaultPBRCS

// One float4 per parameter in Content/Materials/Schemes/DefaultPBR.json, in declaration order.
// Scalars live in .x and the rest of the slot is padding -- see MaterialScheme::ComputeLayout for
// why the layout is this blunt.
struct DefaultPBRParams {
    float4 vAmbientScale;
    float4 vLightIntensity;
};

[numthreads(kShadeGroupSize, 1, 1)]
void DefaultPBRCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel;
    uint materialSlot;
    if (!DeferredResolveThread(dispatchThreadID.x, pixel, materialSlot))
        return;

    DeferredSurface surface = DeferredLoadSurface(pixel, materialSlot);
    DefaultPBRParams params = DEFERRED_LOAD_SCHEME_PARAMS(DefaultPBRParams, materialSlot);

    // Only the lights that can reach this pixel: the directional ones, which reach everything, plus
    // whatever the pixel's cluster holds (ClusteredLightPass). DeferredBeginLights is the only thing
    // that knows about the grid -- with scene.clustered_lights off it hands back every light in the
    // scene and this loop is the naive one it replaced, which is exactly how the two get compared.
    // The type branch lives in LightEvaluate rather than in a shader variant.
    DeferredLightList lights = DeferredBeginLights(surface.vWorldPosition, pixel);

    float3 direct = 0.0f;
    for (uint i = 0; i < DeferredLightListTotal(lights); ++i) {
        float3 lightDir;
        float3 radiance;
        if (!LightEvaluate(DeferredLoadLight(DeferredLightIndex(lights, i)), surface.vWorldPosition, lightDir, radiance))
            continue;

        direct += CookTorrance(surface.vNormal, surface.vViewDirection, lightDir,
                               surface.vAlbedo, surface.fMetallic, surface.fRoughness) * radiance;
    }

    // ambientScale is what keeps a scene with no lights from being pure black.
    float3 ambient = surface.vAlbedo * params.vAmbientScale.x;
    float3 color = direct * params.vLightIntensity.x + ambient + surface.vEmissive;

    // Linear and un-tonemapped: Composite.hlsl owns the transfer curve.
    DeferredWrite(pixel, color);
}
