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

float4 DebugMeshletIDPS(VSOut input) : SV_Target {
    uint id = g_Constants.uInstanceIndex * 9781u + input.uMeshletID;
    return float4(IdToColor(id), 1.0f);
}
