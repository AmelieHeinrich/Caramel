/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:40:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Common/SceneMesh.hlsli"

#pragma pixel DefaultPBRPS

// Mirrors the "parameters" array in Content/Materials/Schemes/DefaultPBR.json, in declaration order.
// The defaults there reproduce the previously hardcoded constants, so the default look is unchanged.
// Tightly packed: offsets 0 and 4, element stride 8. See MaterialScheme::ComputeLayout.
struct DefaultPBRParams {
    float fAmbientScale;
    float fDiffuseScale;
};

float4 DefaultPBRPS(VSOut input) : SV_Target {
    GPUMaterial material = SceneLoadMaterial(input.uMaterialSlot);
    DefaultPBRParams params = SCENE_LOAD_SCHEME_PARAMS(DefaultPBRParams, input.uMaterialSlot);

    AGFXTexture2D<float4> tBaseColor = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureBaseColor]);
    AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);
    float4 baseColor = tBaseColor.Sample(sSampler, input.vUV) * material.vBaseColorFactor;
    if (baseColor.a < 0.1f) {
        discard;
    }

    float3 normal = normalize(input.vWorldNormal);
    float3 lightDir = normalize(float3(-0.4f, 1.0f, -0.3f));
    float ndotl = max(dot(normal, lightDir), 0.0f);

    float3 ambient = baseColor.rgb * params.fAmbientScale;
    float3 diffuse = baseColor.rgb * ndotl * params.fDiffuseScale;
    float3 emissive = material.vEmissiveFactor.rgb;
    float3 final = ambient + diffuse + emissive;
    final = pow(final, 1.0f / 2.2f); // gamma correction

    return float4(final, baseColor.a);
}
