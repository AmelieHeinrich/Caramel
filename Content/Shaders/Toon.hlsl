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

// Stub until deferred shading: the scene now goes through VisBuffer.hlsl + GBufferResolve.hlsl,
// and schemes will be rewritten as deferred shading passes.
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

    float3 emissive = material.vEmissiveFactor.rgb;
    return float4(baseColor.rgb * saturate(params.fBrightness) + emissive, baseColor.a);
}
