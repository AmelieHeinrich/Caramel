/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Stage 3 of clustered light culling: one group per cluster, binning the lights that survived the
// frustum pass.
//
// The group's threads stride the visible-light list in parallel and accumulate hits in groupshared
// memory, so the shared buffer is touched once per cluster instead of once per hit -- the same
// reason the material classification passes batch their atomics per tile rather than per pixel.
//
// Cost is kClusterCount * visibleLightCount sphere-vs-AABB tests, which is why the frustum pass runs
// first: it is the only thing that bounds the inner loop.

#include "Common/ClusterCull.hlsli"

#pragma compute CullLightClustersCS

groupshared uint gsLightCount;
groupshared uint gsLightIndices[kMaxLightsPerCluster];

[numthreads(kClusterGroupSize, 1, 1)]
void CullLightClustersCS(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint clusterIndex = groupID.x;
    uint thread = groupThreadID.x;

    if (thread == 0)
        gsLightCount = 0;
    GroupMemoryBarrierWithGroupSync();

    AGFXRWByteAddressBuffer cull = ClusterCullBuffer();

    // The frustum pass leaves its counter unclamped so the CPU can see an overflow, so clamp here.
    uint visibleCount = min(cull.Load(kLightCullLocalCountBase), g_Constants.uMaxVisibleLights);

    ClusterAABB aabb = AGFXStructuredBuffer<ClusterAABB>::Create(g_Constants.rClusterAABBs).Load(clusterIndex);
    AGFXByteAddressBuffer visible = AGFXByteAddressBuffer::Create(g_Constants.rVisibleLights);
    FrameConstants frame = ClusterLoadFrame();

    for (uint i = thread; i < visibleCount; i += kClusterGroupSize) {
        uint lightIndex = visible.Load(i * 4);
        GPULight light = ClusterLoadLight(lightIndex);

        // The AABBs are in view space, so the light's sphere is transformed rather than the box --
        // one matrix multiply per (cluster, light) against six plane-like comparisons for an
        // oriented world-space test, and the view transform has no scale so the radius carries over
        // unchanged.
        float3 centerVS = mul(frame.mView, float4(light.vPositionRange.xyz, 1.0f)).xyz;
        if (!ClusterTouchesSphere(aabb, centerVS, light.vPositionRange.w))
            continue;

        uint slot;
        InterlockedAdd(gsLightCount, 1, slot);
        if (slot < kMaxLightsPerCluster)
            gsLightIndices[slot] = lightIndex;
    }

    GroupMemoryBarrierWithGroupSync();

    // Unclamped, so a cluster that wanted more than it could hold is still visible to the CPU
    // warning and the heatmap readout. What is stored is clamped.
    uint wanted = gsLightCount;
    uint stored = min(wanted, kMaxLightsPerCluster);

    AGFXRWByteAddressBuffer clusterLights = ClusterLightBuffer();

    if (thread == 0) {
        clusterLights.Store(kClusterCountBase + clusterIndex * 4, stored);
        uint previous;
        cull.InterlockedMax(kLightCullOverflowBase, wanted, previous);
    }

    uint indexBase = ClusterIndexSlotBase(clusterIndex);
    for (uint j = thread; j < stored; j += kClusterGroupSize)
        clusterLights.Store(indexBase + j * 4, gsLightIndices[j]);
}
