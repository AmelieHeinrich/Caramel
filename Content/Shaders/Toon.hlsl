/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Common/SceneMesh.hlsli"

#pragma pixel ToonPS

struct ToonParams {
    float  fBrightness;
    int    iBandCount;
    float4 vRimColor;
    float  fRimPower;
};

float4 ToonPS(VSOut input) : SV_Target {
    SceneLodDither(input);

    GPUMaterial material = SceneLoadMaterial(input.uMaterialSlot);
    ToonParams params = SCENE_LOAD_SCHEME_PARAMS(ToonParams, input.uMaterialSlot);

    AGFXTexture2D<float4> tBaseColor = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureBaseColor]);
    AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);
    float4 baseColor = tBaseColor.Sample(sSampler, input.vUV) * material.vBaseColorFactor;
    if (GPUMaterialIsAlphaTested(material) && baseColor.a < material.fAlphaCutoff) {
        discard;
    }

    float3 normal = normalize(input.vWorldNormal);
    float3 lightDir = normalize(float3(-0.4f, 1.0f, -0.3f));
    float ndotl = max(dot(normal, lightDir), 0.0f);

    // Quantize the diffuse term into discrete bands -- the whole point of the scheme, and what makes
    // the pipeline switch visible at a glance.
    int bands = max(params.iBandCount, 1);
    float banded = floor(ndotl * bands) / bands;
    banded = saturate(banded * params.fBrightness);

    float3 shaded = baseColor.rgb * (0.25f + 0.75f * banded);

    // Cheap view-independent rim: brighten where the surface turns away from the light.
    float rim = pow(saturate(1.0f - ndotl), max(params.fRimPower, 1.0f));
    shaded += params.vRimColor.rgb * rim * params.vRimColor.a;

    float3 emissive = material.vEmissiveFactor.rgb;
    return pow(float4(shaded + emissive, baseColor.a), 1.0 / 2.2);
}
