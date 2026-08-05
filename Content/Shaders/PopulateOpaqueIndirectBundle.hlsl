/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Populates the Opaque indirect bundle: one DrawMesh command per resident scene instance that
// passes frustum culling against the instance's world-space AABB.

#include "Common/AGFX.hlsli"
#include "Common/GPUScene.hlsli"

#pragma compute PopulateOpaqueIndirectBundleCS

struct FrameConstants {
    float4x4 mView;
    float4x4 mProjection;
    float4x4 mViewProjection;
    float4x4 mInvView;
    float4x4 mInvProjection;
    float4x4 mInvViewProjection;
    float4   vFrustumPlanes[6]; // Left/Right/Bottom/Top/Near/Far, xyz = normal, w = distance
    float3   vCameraPosition;
    float    fNearPlane;
    float    fFarPlane;
    float3   _Pad;
};

struct PopulatePushConstants {
    ResourceHandle rInstanceBuffer;
    uint           uInstanceCount;
    uint64_t       uBundleHandle;
    ResourceHandle rDrawIndirection; // valid on every backend; only read/written on Vulkan
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceLodTable;    // GPULodInfo per (instance, lod) -- see GPUScene.hlsli
    ResourceHandle rSelectedLodBuffer;   // uint per instance; this shader writes it
    float          fLodBaseDistance;     // mirrors scene.lod_base_distance
    float          fLodDistanceMultiplier; // mirrors scene.lod_distance_multiplier
    ResourceHandle rMaterialBuffer;      // GPUMaterial per slot; read for the doubleSided flag
    uint           uRegionCapacity;      // commandOffset of the double-sided region (region 1)
};
AGFX_PUSH_CONSTANTS(PopulatePushConstants, g_Constants);

// Transforming just the min/max corners through a rotated instance transform does not yield the
// world-space AABB (it only forms a valid box for axis-aligned/translation-only transforms) --
// InstanceSpawner.as's randomRotation option produces exactly the rotated transforms that broke
// this. Re-derive the world AABB from all 8 local corners instead.
void ComputeWorldBounds(GPUInstance instance, out float3 worldMin, out float3 worldMax)
{
    float3 localMin = instance.vBoundsMin.xyz;
    float3 localMax = instance.vBoundsMax.xyz;

    worldMin = float3(3.402823466e+38, 3.402823466e+38, 3.402823466e+38);
    worldMax = -worldMin;

    [unroll]
    for (int corner = 0; corner < 8; ++corner) {
        float3 localCorner = float3(
            (corner & 1) ? localMax.x : localMin.x,
            (corner & 2) ? localMax.y : localMin.y,
            (corner & 4) ? localMax.z : localMin.z);
        float3 worldCorner = mul(instance.mTransform, float4(localCorner, 1.0)).xyz;
        worldMin = min(worldMin, worldCorner);
        worldMax = max(worldMax, worldCorner);
    }
}

bool IsInstanceVisible(float3 worldMin, float3 worldMax, FrameConstants frame)
{
    [unroll]
    for (int plane = 0; plane < 6; ++plane) {
        float4 frustumPlane = frame.vFrustumPlanes[plane];
        float3 normal = frustumPlane.xyz;
        float distance = frustumPlane.w;

        // Compute the positive vertex of the AABB for this plane.
        float3 positiveVertex = worldMin;
        if (normal.x >= 0) positiveVertex.x = worldMax.x;
        if (normal.y >= 0) positiveVertex.y = worldMax.y;
        if (normal.z >= 0) positiveVertex.z = worldMax.z;

        // If the positive vertex is outside the frustum plane, the AABB is not visible
        if (dot(normal, positiveVertex) + distance < 0) {
            return false;
        }
    }
    return true;
}

[numthreads(64, 1, 1)]
void PopulateOpaqueIndirectBundleCS(uint3 dtid : SV_DispatchThreadID) {
    uint index = dtid.x;
    if (index >= g_Constants.uInstanceCount)
        return;

    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(index);
    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants).Load(0);

    float3 worldMin, worldMax;
    ComputeWorldBounds(instance, worldMin, worldMax);
    if (!IsInstanceVisible(worldMin, worldMax, frame))
        return;

    // Stateless distance-threshold LOD ladder -- no hysteresis (deferred to a future dithered
    // cross-fade). Recomputed fresh every frame. desiredLod starts at the finest LOD and drops one
    // level for every boundary the distance exceeds; boundary b separates LOD (kLodCount-1-b) from
    // LOD (kLodCount-2-b), at threshold fLodBaseDistance * fLodDistanceMultiplier^b.
    float3 instanceCenter = (worldMin + worldMax) * 0.5f;
    float distance = length(instanceCenter - frame.vCameraPosition);

    uint desiredLod = kLodCount - 1;
    [unroll]
    for (uint b = 0; b < kLodCount - 1; ++b) {
        float threshold = g_Constants.fLodBaseDistance * pow(g_Constants.fLodDistanceMultiplier, (float)b);
        if (distance > threshold)
            desiredLod = min(desiredLod, kLodCount - 2 - b);
    }

    // Clamp to this instance's residency ceiling -- data for lod > instance.uLod isn't uploaded yet.
    uint finalLod = min(desiredLod, instance.uLod);

    GPULodInfo lodInfo = SceneLoadLodInfo(g_Constants.rInstanceLodTable, index, finalLod);

    AGFXRWStructuredBuffer<uint> selectedLod = AGFXRWStructuredBuffer<uint>::Create(g_Constants.rSelectedLodBuffer);
    selectedLod.Store(index, finalLod);

    // The consuming pipeline has a task shader (SceneMesh.hlsli's SceneAS) in front of the mesh
    // shader, so this group count dispatches task groups, not mesh groups directly -- one task
    // group per up-to-kMeshletTaskGroupSize candidate meshlets of the chosen LOD.
    uint taskGroupCount = (lodInfo.uMeshletCount + kMeshletTaskGroupSize - 1) / kMeshletTaskGroupSize;

    // Cull mode is fixed-function pipeline state, so double-sided materials cannot share a
    // backface-culling pipeline with single-sided ones. The bundle is split into two regions
    // (SKILL.md gotcha 4): region 0 (offset 0, count slot 0) replays with CullMode::Back, region 1
    // (offset uRegionCapacity, count slot 1) with CullMode::None.
    GPUMaterial material = AGFXStructuredBuffer<GPUMaterial>::Create(g_Constants.rMaterialBuffer).Load(instance.uMaterialSlot);
    bool doubleSided = GPUMaterialIsDoubleSided(material);
    uint countIndex = doubleSided ? 1 : 0;
    uint commandOffset = doubleSided ? g_Constants.uRegionCapacity : 0;

    AGFXIndirectDrawMeshBundle bundle = AGFXIndirectDrawMeshBundle::Create(g_Constants.uBundleHandle);
    uint slot = bundle.DrawMesh(commandOffset, countIndex, index, taskGroupCount, 1, 1);

#if defined(AGFX_VULKAN)
    // Vulkan's AGFX_DRAW_ID() in the consuming mesh shader is the linear slot, not the drawId
    // written above -- record slot -> index so the consumer can recover it (SKILL.md gotcha 6).
    // The slot is region-relative, so offset by the region start; the consumer gets the same base
    // through its per-region push constants (uDrawIndirectionBase).
    AGFXRWByteAddressBuffer indirection = AGFXRWByteAddressBuffer::Create(g_Constants.rDrawIndirection);
    indirection.Store((commandOffset + slot) * 4, index);
#endif
}
