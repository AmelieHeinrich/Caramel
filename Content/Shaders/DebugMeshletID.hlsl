/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Geometry-only debug pass: colors every meshlet by a hash of (instance, meshlet) so culling and
// LOD work can be visually verified without touching the material system at all.

#include "Common/SceneMesh.hlsli"

#pragma pixel DebugMeshletIDPS

uint WangHash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16);
    seed *= 9u;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15);
    return seed;
}

float3 IdToColor(uint id) {
    uint h = WangHash(id);
    return float3(
        (float)(h & 0xFFu) / 255.0f,
        (float)((h >> 8) & 0xFFu) / 255.0f,
        (float)((h >> 16) & 0xFFu) / 255.0f);
}

float4 DebugMeshletIDPS(VSOut input, PrimOut prim) : SV_Target {
    SceneLodDither(input);

    GPUMaterial material = SceneLoadMaterial(input.uMaterialSlot);
    if (GPUMaterialIsAlphaTested(material)) {
        AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);
        AGFXTexture2D<float4> tBaseColor = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureBaseColor]);
        float alpha = tBaseColor.Sample(sSampler, input.vUV).a * material.vBaseColorFactor.a;
        if (alpha < material.fAlphaCutoff)
            discard;
    }

    uint meshletId = input.uInstanceIndex * 9781u + input.uMeshletID;

    uint id = meshletId;
    if (g_Constants.uDebugId == kSceneDebugIdPrimitive)
        id = meshletId * 4139u + prim.uPrimitiveID;
    else if (g_Constants.uDebugId == kSceneDebugIdInstance)
        id = input.uInstanceIndex;
    else if (g_Constants.uDebugId == kSceneDebugIdLod) {
        // Red = coarsest .. blue = finest; a cross-fade shows as two colors dissolving into each other.
        const float3 lodColors[kLodCount] = {
            float3(1.0f, 0.2f, 0.2f),
            float3(1.0f, 0.6f, 0.1f),
            float3(1.0f, 1.0f, 0.2f),
            float3(0.3f, 1.0f, 0.3f),
            float3(0.3f, 0.6f, 1.0f),
        };
        return float4(lodColors[min(input.uLOD, kLodCount - 1)], 1.0f);
    }
    
    // Fake light
    float3 normal = normalize(input.vWorldNormal);
    float3 lightDir = normalize(float3(1.0f, 1.0f, 1.0f));
    float light = saturate(dot(normal, lightDir)) * 0.5f + 0.5f;

    return float4(input.vWorldNormal * 0.5 + 0.5, 1.0f);
}
