/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Stage 2 of clustered light culling: one thread per cluster, writing its view-space AABB.
//
// A cluster is the intersection of a screen-space tile with a depth slice, which in view space is a
// truncated pyramid. Its AABB is the box around the tile's four corner rays evaluated at the slice's
// near and far depths -- conservative, since the box contains the frustum, which is the safe
// direction: a too-large cluster costs a few extra LightEvaluate calls, a too-small one drops light.
//
// The grid only depends on the projection, so this could be cached and rebuilt on viewport change.
// It is not, because it is 3456 threads -- 54 groups, less work than a single screen tile of the
// classification pass -- and a cache is one more thing that can be stale when the camera changes.

#include "Common/ClusterCull.hlsli"

#pragma compute BuildLightClustersCS

// The tile's corner ray, in view space, as a direction from the camera. Unprojected at NDC z = 1
// (the far plane under GLM_FORCE_DEPTH_ZERO_TO_ONE, which xmake defines) -- only the direction
// matters, the depth is imposed below.
float3 ClusterCornerRay(float2 ndc, float4x4 invProjection)
{
    float4 view = mul(invProjection, float4(ndc, 1.0f, 1.0f));
    return view.xyz / view.w;
}

[numthreads(kClusterGroupSize, 1, 1)]
void BuildLightClustersCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint clusterIndex = dispatchThreadID.x;
    if (clusterIndex >= kClusterCount)
        return;

    uint3 cluster;
    cluster.x = clusterIndex % kClusterGridX;
    cluster.y = (clusterIndex / kClusterGridX) % kClusterGridY;
    cluster.z = clusterIndex / (kClusterGridX * kClusterGridY);

    // Resetting the count here rather than from a zero-buffer copy: this pass already touches every
    // cluster exactly once. It also makes an empty grid the failure mode if the cluster culling
    // kernel is mid-hot-reload and its dispatch is skipped, instead of last frame's stale lists.
    ClusterLightBuffer().Store(kClusterCountBase + clusterIndex * 4, 0);

    FrameConstants frame = ClusterLoadFrame();

    // Tile bounds in NDC. Cluster Y is indexed top-down like a pixel coordinate, while NDC Y points
    // up, so the two Y edges swap -- ClusterFromPixel indexes the same way, and this is where the
    // two have to agree.
    float2 ndcMin = float2( (float)cluster.x       / (float)kClusterGridX * 2.0f - 1.0f,
                     1.0f - (float)(cluster.y + 1) / (float)kClusterGridY * 2.0f);
    float2 ndcMax = float2( (float)(cluster.x + 1) / (float)kClusterGridX * 2.0f - 1.0f,
                     1.0f - (float)cluster.y       / (float)kClusterGridY * 2.0f);

    float3 rays[4];
    rays[0] = ClusterCornerRay(float2(ndcMin.x, ndcMin.y), frame.mInvProjection);
    rays[1] = ClusterCornerRay(float2(ndcMax.x, ndcMin.y), frame.mInvProjection);
    rays[2] = ClusterCornerRay(float2(ndcMin.x, ndcMax.y), frame.mInvProjection);
    rays[3] = ClusterCornerRay(float2(ndcMax.x, ndcMax.y), frame.mInvProjection);

    float zNear = ClusterSliceViewZ(cluster.z,        g_Constants.fSliceScale, g_Constants.fSliceBias);
    float zFar  = ClusterSliceViewZ(cluster.z + 1,    g_Constants.fSliceScale, g_Constants.fSliceBias);

    float3 boundsMin = 1e30f;
    float3 boundsMax = -1e30f;

    [unroll]
    for (uint corner = 0; corner < 4; ++corner) {
        // View space looks down -Z, so a ray's positive depth is -ray.z; scaling by depth / (-ray.z)
        // slides the corner along the ray to exactly that depth.
        float invDepth = 1.0f / max(-rays[corner].z, 1e-6f);
        float3 nearPoint = rays[corner] * (zNear * invDepth);
        float3 farPoint  = rays[corner] * (zFar  * invDepth);

        boundsMin = min(boundsMin, min(nearPoint, farPoint));
        boundsMax = max(boundsMax, max(nearPoint, farPoint));
    }

    ClusterAABB aabb;
    aabb.vMin = float4(boundsMin, 0.0f);
    aabb.vMax = float4(boundsMax, 0.0f);
    AGFXRWStructuredBuffer<ClusterAABB>::Create(g_Constants.rClusterAABBs).Store(clusterIndex, aabb);
}
