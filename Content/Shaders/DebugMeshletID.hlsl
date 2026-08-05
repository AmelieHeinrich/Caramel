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

float3 LODToColor(uint lod) {
    switch (lod) {
        case 0: return float3(1.0f, 0.0f, 0.0f);
        case 1: return float3(0.0f, 1.0f, 0.0f);
        case 2: return float3(0.0f, 0.0f, 1.0f);
        case 3: return float3(1.0f, 1.0f, 0.0f);
        case 4: return float3(1.0f, 0.0f, 1.0f);
        default: return float3(1.0f, 1.0f, 1.0f);
    }
}

float4 DebugMeshletIDPS(VSOut input, PrimOut prim) : SV_Target {
    if ((g_Constants.uDebugFlags & 2u) != 0)
        return float4(LODToColor(input.uLOD), 1.0f);

    uint meshletId = input.uInstanceIndex * 9781u + input.uMeshletID;
    uint id = (g_Constants.uDebugFlags & 1u) != 0
        ? meshletId * 4139u + prim.uPrimitiveID
        : meshletId;

    // Replace with very simple lighting to see LOD pop
    float3 normal = normalize(input.vWorldNormal);
    float3 lightDir = normalize(float3(1.0f, 1.0f, 1.0f));
    float light = saturate(dot(normal, lightDir));

    return float4(IdToColor(id), 1.0f);
}
