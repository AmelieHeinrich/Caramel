/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:40:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Common/SceneMesh.hlsli"

#pragma pixel DefaultPBRPS

// Stub until deferred shading: the scene now goes through VisBuffer.hlsl + GBufferResolve.hlsl,
// and schemes will be rewritten as deferred shading passes. Kept compiling so SchemeRegistry and
// the scheme parameter plumbing stay exercised.

// Mirrors the "parameters" array in Content/Materials/Schemes/DefaultPBR.json, in declaration order.
// See MaterialScheme::ComputeLayout.
struct DefaultPBRParams {
    float fLightIntensity;
};

float4 DefaultPBRPS(VSOut input) : SV_Target {
    SceneLodDither(input);

    GPUMaterial material = SceneLoadMaterial(input.uMaterialSlot);
    DefaultPBRParams params = SCENE_LOAD_SCHEME_PARAMS(DefaultPBRParams, input.uMaterialSlot);

    AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);
    AGFXTexture2D<float4> tBaseColor = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureBaseColor]);
    float4 baseColor = tBaseColor.Sample(sSampler, input.vUV) * material.vBaseColorFactor;
    if (GPUMaterialIsAlphaTested(material) && baseColor.a < material.fAlphaCutoff) {
        discard;
    }

    return float4(baseColor.rgb * params.fLightIntensity, baseColor.a);
}
