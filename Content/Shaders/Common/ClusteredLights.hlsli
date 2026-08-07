/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// The cluster grid, defined once. Everything that touches it -- the three culling kernels, the
// shading kernels through DeferredShading.hlsli, and the heatmap debug view in GBufferResolve.hlsl
// -- speaks this file, so the grid can never be described two different ways.
//
// A frame goes Cull Lights (frustum) -> Build Clusters -> Cull Lights (clusters) -> Material Shade.
// The first pass drops lights the camera cannot see at all and splits off the directional ones, the
// second rebuilds the view-space AABBs, the third bins the survivors, and shading walks only the
// list belonging to its pixel's cluster.

#ifndef CARAMEL_CLUSTEREDLIGHTS_HLSL
#define CARAMEL_CLUSTEREDLIGHTS_HLSL

#include "AGFX.hlsli"
#include "GPUScene.hlsli"

// Doom 2016's grid. Fixed rather than derived from a pixel tile size, which is what makes every
// buffer here a single allocation made once at startup: the cluster count never changes, so a
// viewport resize costs nothing and there is no capacity to grow. The tiles are wider on an
// ultrawide viewport and that is fine -- the grid only decides which lights a pixel considers, and
// a looser tile costs a few extra LightEvaluate calls, never a wrong result.
static const uint kClusterGridX = 16;
static const uint kClusterGridY = 9;
static const uint kClusterGridZ = 24;
static const uint kClusterCount = kClusterGridX * kClusterGridY * kClusterGridZ;   // 3456

// Slots per cluster in the index list. Fixed-stride rather than a compacted global allocation: it
// costs 1.7 MB, which is nothing, and it removes the one global atomic and the overflow policy that
// a shared list would need. A cluster that wants more than this clamps -- see kClusterOverflowBase.
static const uint kMaxLightsPerCluster = 128;

// Directional lights are unbounded (range 0), so they belong to every cluster and are never binned.
// The frustum pass sorts them into their own short list that every pixel walks in full.
static const uint kMaxDirectionalLights = 8;

// Threads per group, shared by all three kernels. The cluster culling kernel is one group per
// cluster, so this is also how many lights it tests in parallel per cluster.
static const uint kClusterGroupSize = 64;

// --- m_ClusterLights layout ------------------------------------------------------------------
// Counts then indices in one raw allocation, the same trick as kClassify*Base in
// DeferredShading.hlsli. Raw and not structured because only AGFXRWByteAddressBuffer carries the
// Interlocked* family. Byte offsets throughout.
static const uint kClusterCountBase = 0;
static const uint kClusterIndexBase = kClusterCount * 4;
static const uint kClusterLightWords = kClusterCount + kClusterCount * kMaxLightsPerCluster;

// --- m_LightCull layout ----------------------------------------------------------------------
// The frustum pass's two counters plus the directional index list. Byte offsets.
static const uint kLightCullDirectionalCountBase = 0;
static const uint kLightCullLocalCountBase       = 4;
// Highest lights-per-cluster any cluster wanted this frame, InterlockedMax'd by the cluster pass.
// Purely diagnostic -- it is what the heatmap's overflow readout and the capacity warning read.
static const uint kLightCullOverflowBase         = 8;
static const uint kLightCullDirectionalBase      = 16;
static const uint kLightCullWords = 4 + kMaxDirectionalLights;

uint ClusterIndex(uint3 cluster) {
    return (cluster.z * kClusterGridY + cluster.y) * kClusterGridX + cluster.x;
}

// Byte offset of cluster c's first index slot.
uint ClusterIndexSlotBase(uint cluster) {
    return kClusterIndexBase + cluster * kMaxLightsPerCluster * 4;
}

// The depth slicing is exponential, so slices are thin near the camera where clusters are small in
// world space and thick far away where they are not. scale/bias fold the log-space remap into a
// single madd and are computed CPU-side from the camera's near/far -- see ClusteredLightPass.
//
// viewZ here and everywhere below is *positive* distance along the view axis. glm::lookAt is
// right-handed (Camera.cpp), so view space looks down -Z and this is -viewPosition.z. Both the AABB
// build and the pixel lookup go through these helpers precisely so that sign is written once.
uint ClusterSliceFromViewZ(float viewZ, float sliceScale, float sliceBias) {
    float slice = log2(max(viewZ, 1e-4f)) * sliceScale + sliceBias;
    return (uint)clamp(slice, 0.0f, (float)(kClusterGridZ - 1));
}

// The near and far view-space depths bounding slice z. Inverse of ClusterSliceFromViewZ, which is
// what the AABB build needs.
float ClusterSliceViewZ(uint slice, float sliceScale, float sliceBias) {
    return exp2(((float)slice - sliceBias) / sliceScale);
}

uint ClusterFromPixel(uint2 pixel, float viewZ, uint width, uint height, float sliceScale, float sliceBias) {
    uint3 cluster;
    cluster.x = min((uint)((float)pixel.x * (float)kClusterGridX / (float)max(width, 1u)), kClusterGridX - 1);
    cluster.y = min((uint)((float)pixel.y * (float)kClusterGridY / (float)max(height, 1u)), kClusterGridY - 1);
    cluster.z = ClusterSliceFromViewZ(viewZ, sliceScale, sliceBias);
    return ClusterIndex(cluster);
}

// One cluster's view-space bounds. float4 rather than float3 so the C++ side (ClusteredLightPass)
// and this layout are trivially identical -- the same reason GPUInstance's bounds are float4.
struct ClusterAABB {
    float4 vMin;    // w unused
    float4 vMax;    // w unused
};

// Does a light's bounding sphere touch this cluster? The standard closest-point-on-box test: clamp
// the centre into the box and compare the squared distance against the radius. Exact, and it is what
// makes the range in SceneLight a real culling primitive -- LightEvaluate's windowed falloff reaches
// zero exactly at range, so a light rejected here contributes exactly nothing, with no seam.
bool ClusterTouchesSphere(ClusterAABB aabb, float3 vCenter, float fRadius) {
    float3 closest = clamp(vCenter, aabb.vMin.xyz, aabb.vMax.xyz);
    float3 delta = closest - vCenter;
    return dot(delta, delta) <= fRadius * fRadius;
}

#endif
