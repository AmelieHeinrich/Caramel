/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Occlusion test against the max-reduction depth pyramid BuildHZB.hlsl produces. Shared by
// PopulateOpaqueIndirectBundle.hlsl (per instance AABB) and SceneMesh.hlsli's SceneAS (per meshlet
// bounding sphere, boxed).

#ifndef CARAMEL_HZB_HLSL
#define CARAMEL_HZB_HLSL

#include "AGFX.hlsli"

struct HZBParams {
    ResourceHandle rTexture;
    uint2          uSize;    // mip 0 dimensions
    uint           uMipCount;
};

HZBParams HZBMakeParams(ResourceHandle texture, uint width, uint height, uint mipCount) {
    HZBParams params;
    params.rTexture = texture;
    params.uSize = uint2(width, height);
    params.uMipCount = mipCount;
    return params;
}

// Projects the world-space box through viewProj and asks whether every surface already in the pyramid
// under its screen rect is closer than the box's nearest corner. Depth is non-reversed-Z, so the
// pyramid holds the *farthest* depth per region and "occluded" means minZ > that farthest value.
//
// Deliberately projects all 8 corners rather than reusing SceneMesh.hlsli's SphereScreenExtents --
// that helper's own header flags its left/right/bottom/top signs as unverified against this engine's
// projection layout, and an over-eager occlusion cull is a much louder bug than an over-eager
// contribution cull.
bool HZBIsOccluded(float3 worldMin, float3 worldMax, float4x4 mViewProjection, HZBParams hzb) {
    float3 ndcMin = float3(3.402823466e+38, 3.402823466e+38, 3.402823466e+38);
    float3 ndcMax = -ndcMin;

    [unroll]
    for (int corner = 0; corner < 8; ++corner) {
        float3 worldCorner = float3(
            (corner & 1) ? worldMax.x : worldMin.x,
            (corner & 2) ? worldMax.y : worldMin.y,
            (corner & 4) ? worldMax.z : worldMin.z);

        float4 clip = mul(mViewProjection, float4(worldCorner, 1.0));

        // Straddling or behind the near plane: the perspective divide stops meaning anything and the
        // screen rect would be garbage. Never cull in that case.
        if (clip.w <= 0.0)
            return false;

        float3 ndc = clip.xyz / clip.w;
        ndcMin = min(ndcMin, ndc);
        ndcMax = max(ndcMax, ndc);
    }

    // Anything poking through the near plane is already handled above, so a box entirely in front of
    // the camera can still be entirely outside the frustum sideways -- frustum culling deals with
    // that, and clamping the rect here just makes the sample coordinates well-defined.
    float2 uvMin = saturate(float2(ndcMin.x, -ndcMax.y) * 0.5 + 0.5);
    float2 uvMax = saturate(float2(ndcMax.x, -ndcMin.y) * 0.5 + 0.5);

    float2 size = (float2)hzb.uSize;
    float2 texelMin = uvMin * size;
    float2 texelMax = uvMax * size;

    // Pick the coarsest-but-one level at which the rect spans no more than 2x2 texels, so the four
    // loads below cover it completely.
    float extent = max(texelMax.x - texelMin.x, texelMax.y - texelMin.y);
    int mip = (int)ceil(log2(max(extent, 1.0)));
    mip = clamp(mip, 0, (int)hzb.uMipCount - 1);

    uint2 mipSize = uint2(max(hzb.uSize.x >> (uint)mip, 1u), max(hzb.uSize.y >> (uint)mip, 1u));
    int2 coordMin = clamp((int2)(texelMin / exp2((float)mip)), int2(0, 0), (int2)mipSize - 1);
    int2 coordMax = clamp((int2)(texelMax / exp2((float)mip)), coordMin, (int2)mipSize - 1);

    Texture2D<float> pyramid = ResourceDescriptorHeap[hzb.rTexture];
    float farthest = pyramid.Load(int3(coordMin.x, coordMin.y, mip));
    farthest = max(farthest, pyramid.Load(int3(coordMax.x, coordMin.y, mip)));
    farthest = max(farthest, pyramid.Load(int3(coordMin.x, coordMax.y, mip)));
    farthest = max(farthest, pyramid.Load(int3(coordMax.x, coordMax.y, mip)));

    return ndcMin.z > farthest;
}

#endif
