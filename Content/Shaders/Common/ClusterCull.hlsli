/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// The producer half of clustered light culling: what LightCullFrustum, BuildLightClusters and
// CullLightClusters share. Split out of ClusteredLights.hlsli because that file is also included by
// DeferredShading.hlsli, and a translation unit can only declare one push-constant block -- the
// consumer side has its own.

#ifndef CARAMEL_CLUSTERCULL_HLSL
#define CARAMEL_CLUSTERCULL_HLSL

#include "AGFX.hlsli"
#include "ClusteredLights.hlsli"
#include "SceneGeometry.hlsli"

// Mirrors ClusterPushConstants in Sources/Caramel/Renderer/Passes/ClusteredLightPass.cpp. All
// scalars, deliberately: push constants pack by cbuffer rules, so a vector field can be bumped past
// a 16-byte boundary and silently shift everything after it (same warning as DeferredShading.hlsli).
struct ClusterPushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rLightBuffer;       // stale or null when uLightCount is 0 -- never read unguarded
    uint           uLightCount;
    ResourceHandle rLightCull;         // counters + directional index list, see kLightCull*Base
    ResourceHandle rVisibleLights;     // frustum survivors, one uint per light
    ResourceHandle rClusterAABBs;      // ClusterAABB per cluster, view space
    ResourceHandle rClusterLights;     // counts + per-cluster index lists, see kCluster*Base
    float          fSliceScale;
    float          fSliceBias;
    uint           uMaxVisibleLights;  // capacity of rVisibleLights, i.e. this frame's light count
};
AGFX_PUSH_CONSTANTS(ClusterPushConstants, g_Constants);

FrameConstants ClusterLoadFrame() {
    return AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants).Load(0);
}

GPULight ClusterLoadLight(uint index) {
    return AGFXStructuredBuffer<GPULight>::Create(g_Constants.rLightBuffer).Load(index);
}

AGFXRWByteAddressBuffer ClusterCullBuffer() {
    return AGFXRWByteAddressBuffer::Create(g_Constants.rLightCull);
}

AGFXRWByteAddressBuffer ClusterLightBuffer() {
    return AGFXRWByteAddressBuffer::Create(g_Constants.rClusterLights);
}

#endif
