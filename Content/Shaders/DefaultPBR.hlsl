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

// Placeholder until the light list exists (Notes/TODO.md, "Base lighting pass"). Angled rather than
// straight up so surfaces of different orientation are actually distinguishable.
static const float3 kSunDirection = float3(0.0f, 1.0f, 0.0f);

// The forward pass this replaces hardcoded a 5.0 multiplier on the direct term (see
// `git show 91b9ed8^:Content/Shaders/DefaultPBR.hlsl`). Kept as the sun's radiance so the scheme's
// lightIntensity parameter stays a ~1.0-centred multiplier rather than having to carry the exposure.
static const float kSunRadiance = 5.0f;

[numthreads(kShadeGroupSize, 1, 1)]
void DefaultPBRCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel;
    uint materialSlot;
    if (!DeferredResolveThread(dispatchThreadID.x, pixel, materialSlot))
        return;

    DeferredSurface surface = DeferredLoadSurface(pixel, materialSlot);
    DefaultPBRParams params = DEFERRED_LOAD_SCHEME_PARAMS(DefaultPBRParams, materialSlot);

    float3 lightDir = normalize(kSunDirection);
    float3 direct = CookTorrance(surface.vNormal, surface.vViewDirection, lightDir,
                                 surface.vAlbedo, surface.fMetallic, surface.fRoughness);

    float3 ambient = surface.vAlbedo * params.vAmbientScale.x;
    float3 color = direct * (kSunRadiance * params.vLightIntensity.x) + ambient + surface.vEmissive;

    // Linear and un-tonemapped: Composite.hlsl owns the transfer curve.
    DeferredWrite(pixel, color);
}
