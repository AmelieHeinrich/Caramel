/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Which lights one shaded pixel is allowed to consider, parameterised on explicit handles rather
// than on a push-constant block.
//
// Split out of DeferredShading.hlsli for the same reason as GBufferSurface.hlsli: a translation unit
// may only declare one push-constant block, so an alternative shading path cannot include that
// header. Duplicating this walk would be worse than the split -- ReSTIR's initial candidate
// generation has to draw from *exactly* the set the reference path evaluates, or the estimator is
// biased against an image it can never converge to.

#ifndef CARAMEL_LIGHTLIST_HLSL
#define CARAMEL_LIGHTLIST_HLSL

#include "AGFX.hlsli"
#include "SceneGeometry.hlsli"
#include "ClusteredLights.hlsli"

// Clustered, this is the pixel's cluster's list plus the directional lights, which are unbounded and
// therefore never binned. Unclustered (scene.clustered_lights off), it is simply every light in the
// scene -- the ground truth the clustered path must match exactly. Both are walked identically by
// the caller, which is the point: a kernel says which lights it walks, never how the set was chosen.
struct DeferredLightList {
    uint uDirectionalCount;   // 0 when unclustered -- the brute-force walk already includes them
    uint uLocalCount;
    uint uClusterIndexBase;   // byte offset of the cluster's first index slot
};

// The handle set the walk reads. Filled by a caller from its own push constants; field names match
// the push-constant fields on both sides so the mapping stays a one-liner.
struct LightListHandles {
    ResourceHandle rFrameConstants;
    ResourceHandle rClusterLights;   // per-cluster counts, then per-cluster index lists
    ResourceHandle rLightCull;       // counters + the directional index list
    float          fSliceScale;
    float          fSliceBias;
    uint           uClusteringEnabled;
    uint           uLightCount;
};

uint DeferredLightListTotal(DeferredLightList list) {
    return list.uDirectionalCount + list.uLocalCount;
}

DeferredLightList LightListBegin(LightListHandles h, float3 vWorldPosition, uint2 pixel, uint width, uint height) {
    DeferredLightList list;
    list.uDirectionalCount = 0;
    list.uLocalCount = h.uLightCount;
    list.uClusterIndexBase = 0;

    if (h.uClusteringEnabled == 0 || h.uLightCount == 0)
        return list;

    // The grid is sliced on view depth, which is the distance along the view axis -- not the distance
    // to the camera, and not the depth buffer's value. View space looks down -Z (glm::lookAt is
    // right-handed), so this is the negated view-space Z.
    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(h.rFrameConstants).Load(0);
    float viewZ = -mul(frame.mView, float4(vWorldPosition, 1.0f)).z;

    uint cluster = ClusterFromPixel(pixel, viewZ, width, height, h.fSliceScale, h.fSliceBias);

    AGFXByteAddressBuffer clusterLights = AGFXByteAddressBuffer::Create(h.rClusterLights);
    AGFXByteAddressBuffer cull = AGFXByteAddressBuffer::Create(h.rLightCull);

    // The frustum pass leaves its directional counter unclamped so the CPU can warn about it.
    list.uDirectionalCount = min(cull.Load(kLightCullDirectionalCountBase), kMaxDirectionalLights);
    list.uLocalCount = clusterLights.Load(kClusterCountBase + cluster * 4);
    list.uClusterIndexBase = ClusterIndexSlotBase(cluster);
    return list;
}

// Index i of the list into the scene's light buffer. Directional lights come first so the caller can
// walk one flat range.
uint LightListIndex(LightListHandles h, DeferredLightList list, uint i) {
    if (h.uClusteringEnabled == 0)
        return i;

    if (i < list.uDirectionalCount)
        return AGFXByteAddressBuffer::Create(h.rLightCull).Load(kLightCullDirectionalBase + i * 4);

    uint local = i - list.uDirectionalCount;
    return AGFXByteAddressBuffer::Create(h.rClusterLights).Load(list.uClusterIndexBase + local * 4);
}

#endif
