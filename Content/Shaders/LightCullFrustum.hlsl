/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Stage 1 of clustered light culling: one thread per light, tested against the camera frustum.
//
// Two outputs, because lights fall into two kinds. A directional light has no position and no range
// -- it reaches every pixel in the frame -- so it is never binned into a cluster; it goes into a
// short list that every shaded pixel walks in full. Everything else has a real bounding sphere, gets
// tested against the frustum here, and the survivors are compacted into rVisibleLights for the
// cluster pass to bin.
//
// Doing this before the cluster binning matters more than it looks: the cluster pass is
// kClusterCount groups each walking the whole surviving list, so every light dropped here is 3456
// sphere-vs-AABB tests that never happen.

#include "Common/ClusterCull.hlsli"

#pragma compute LightCullFrustumCS

[numthreads(kClusterGroupSize, 1, 1)]
void LightCullFrustumCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint lightIndex = dispatchThreadID.x;
    if (lightIndex >= g_Constants.uLightCount)
        return;

    GPULight light = ClusterLoadLight(lightIndex);
    AGFXRWByteAddressBuffer cull = ClusterCullBuffer();

    if (light.uType == kLightTypeDirectional) {
        uint slot;
        cull.InterlockedAdd(kLightCullDirectionalCountBase, 1, slot);
        // The counter is left unclamped on purpose: the CPU reads it back to warn about a scene with
        // more suns than the list holds. Readers clamp to kMaxDirectionalLights.
        if (slot < kMaxDirectionalLights)
            cull.Store(kLightCullDirectionalBase + slot * 4, lightIndex);
        return;
    }

    // Same test, same sign convention as FrustumCullMeshlet in SceneMesh.hlsli -- the planes point
    // inwards, so a sphere is out when its centre is more than a radius behind any of them. Reads
    // the frozen planes, so scene.freeze_frustum freezes light culling exactly like meshlet culling.
    FrameConstants frame = ClusterLoadFrame();
    float3 center = light.vPositionRange.xyz;
    float radius = light.vPositionRange.w;

    [unroll]
    for (int plane = 0; plane < 6; ++plane) {
        float4 frustumPlane = frame.vFrustumPlanes[plane];
        if (dot(frustumPlane.xyz, center) + frustumPlane.w < -radius)
            return;
    }

    uint visibleSlot;
    cull.InterlockedAdd(kLightCullLocalCountBase, 1, visibleSlot);
    if (visibleSlot < g_Constants.uMaxVisibleLights)
        AGFXRWByteAddressBuffer::Create(g_Constants.rVisibleLights).Store(visibleSlot * 4, lightIndex);
}
