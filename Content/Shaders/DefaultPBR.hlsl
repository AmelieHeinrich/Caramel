/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:40:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Common/SceneMesh.hlsli"
#include "Common/BRDF.hlsli"

#pragma pixel DefaultPBRPS

// Mirrors the "parameters" array in Content/Materials/Schemes/DefaultPBR.json, in declaration order.
// See MaterialScheme::ComputeLayout.
struct DefaultPBRParams {
    float fLightIntensity;
};

float4 DefaultPBRPS(VSOut input) : SV_Target {
    GPUMaterial material = SceneLoadMaterial(input.uMaterialSlot);
    DefaultPBRParams params = SCENE_LOAD_SCHEME_PARAMS(DefaultPBRParams, input.uMaterialSlot);

    AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);

    AGFXTexture2D<float4> tBaseColor = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureBaseColor]);
    float4 baseColor = tBaseColor.Sample(sSampler, input.vUV) * material.vBaseColorFactor;
    if (baseColor.a < 0.1f) {
        discard;
    }

    // An overridden channel takes the factor as-is -- multiplying it into the texture would still
    // leave a 0 texel at 0 no matter the factor, defeating the point of forcing a value.
    float roughness = material.fRoughnessFactor;
    float metallic = material.fMetallicFactor;
    if (material.uTextures[kMaterialTextureMetallicRoughness] != g_Constants.rFallbackTexture) {
        AGFXTexture2D<float4> tMetallicRoughness = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureMetallicRoughness]);
        float4 metallicRoughness = tMetallicRoughness.Sample(sSampler, input.vUV);
        if (!GPUMaterialOverridesRoughness(material))
            roughness = saturate(metallicRoughness.g) * material.fRoughnessFactor;
        if (!GPUMaterialOverridesMetallic(material))
            metallic = saturate(metallicRoughness.b) * material.fMetallicFactor;
    }
    roughness = saturate(roughness);
    metallic = saturate(metallic);

    float3 geometricNormal = normalize(input.vWorldNormal);
    float3 normal = geometricNormal;

    if (material.uTextures[kMaterialTextureNormal] != g_Constants.rFallbackTexture) {
        float3 tangent = normalize(input.vWorldTangent.xyz);
        tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
        float3 bitangent = cross(geometricNormal, tangent) * input.vWorldTangent.w;

        AGFXTexture2D<float4> tNormal = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureNormal]);
        float2 tangentNormalXY = tNormal.Sample(sSampler, input.vUV).rg * 2.0f - 1.0f;
        float tangentNormalZ = sqrt(saturate(1.0f - dot(tangentNormalXY, tangentNormalXY)));
        float3 tangentNormal = float3(tangentNormalXY, tangentNormalZ);
        float3x3 mTBN = float3x3(tangent, bitangent, geometricNormal);
        normal = normalize(mul(tangentNormal, mTBN));
    }

    AGFXStructuredBuffer<FrameConstants> bFrame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants);
    float3 cameraPosition = bFrame.Load(0).vCameraPosition;

    float3 viewDir = normalize(cameraPosition - input.vWorldPosition);
    float3 lightDir = normalize(float3(0.0f, 1.0f, 0.0f));

    float3 directLighting = CookTorrance(normal, viewDir, lightDir, baseColor.rgb, metallic, roughness) * 5.0f;
    float3 emissive = material.vEmissiveFactor.rgb;
    float3 final = directLighting + emissive;
    final = pow(final, 1.0f / 2.2f);

    return float4(final, baseColor.a);
}
