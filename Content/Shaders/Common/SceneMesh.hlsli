/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Geometry half of every material scheme. A scheme's .hlsl includes this and adds only its own
// parameter struct and pixel function -- the meshlet unpacking and vertex transform are identical
// for all of them.
//
// The `#pragma mesh` below is picked up even though it lives in an include: ShaderParser inlines
// includes before it scans for pragmas.

#ifndef CARAMEL_SCENEMESH_HLSL
#define CARAMEL_SCENEMESH_HLSL

#include "AGFX.hlsli"
#include "GPUScene.hlsli"

#pragma task SceneAS
#pragma mesh SceneMS

AGFX_DECLARE_DRAW_ID();

// Mirrors CaramelAsset::Vertex (Sources/CaramelAsset/Format.hpp) field-for-field.
struct Vertex {
    float3 vPosition;
    float3 vNormal;
    float4 vTangent; // xyz = tangent, w = handedness
    float2 vUV;
};

// Mirrors CaramelAsset::MeshletDesc / meshopt_Meshlet exactly.
struct MeshletDesc {
    uint uVertexOffset;
    uint uTriangleOffset; // byte offset into the meshlet-triangle buffer, already 4-byte aligned
    uint uVertexCount;
    uint uTriangleCount;
};

// Mirrors FrameConstants in Sources/Caramel/Renderer/SceneRenderer.cpp field-for-field.
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

// Mirrors ScenePushConstants in Sources/Caramel/Renderer/SceneRenderer.cpp. rSchemeParams points at
// the parameter buffer of the scheme bucket currently being drawn. Draws are submitted through an
// indirect bundle, so the instance index is no longer a per-draw push constant -- it is recovered
// from AGFX_DRAW_ID() (see SceneResolveInstanceIndex below).
struct ScenePushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceBuffer;
    ResourceHandle rMaterialBuffer;
    ResourceHandle rSchemeParams;
    ResourceHandle rDrawIndirection; // valid on every backend; only read on Vulkan
    ResourceHandle rSampler;
    ResourceHandle rFallbackTexture; // handle held by a material texture slot with nothing bound
    uint uDebugFlags; // bit 0: scene.show_primitive_id (see Core/CVar.hpp), read by DebugMeshletID.hlsl
    ResourceHandle rInstanceLodTable;  // GPULodInfo per (instance, lod) -- see GPUScene.hlsli
    ResourceHandle rSelectedLodBuffer; // uint per instance, written by PopulateOpaqueIndirectBundleCS
    uint uDrawIndirectionBase;         // start of this execute's bundle region in rDrawIndirection
                                       // (0 = single-sided, capacity = double-sided); Vulkan only
};
AGFX_PUSH_CONSTANTS(ScenePushConstants, g_Constants);

// AGFX_DRAW_ID() is the value the populate compute shader wrote as drawId on D3D12/Metal, but on
// Vulkan it is the linear position within the compacted indirect bundle instead (SKILL.md gotcha
// 6), so it must be resolved through the indirection buffer the populate shader wrote alongside it.
uint SceneResolveInstanceIndex() {
#if defined(AGFX_VULKAN)
    return AGFXByteAddressBuffer::Create(g_Constants.rDrawIndirection).Load((g_Constants.uDrawIndirectionBase + AGFX_DRAW_ID()) * 4);
#else
    return AGFX_DRAW_ID();
#endif
}

struct VSOut {
    float4 vPosition : SV_POSITION;
    float3 vWorldNormal : NORMAL0;
    float2 vUV : TEXCOORD0;
    nointerpolation uint uMaterialSlot : TEXCOORD1;
    float3 vWorldPosition : TEXCOORD2;
    float4 vWorldTangent : TEXCOORD3;
    nointerpolation uint uMeshletID : TEXCOORD4;
    nointerpolation uint uInstanceIndex : TEXCOORD5;
    nointerpolation uint uLOD : TEXCOORD6;
};

struct PrimOut {
    nointerpolation uint uPrimitiveID : PRIMITIVEID0;
};

GPUMaterial SceneLoadMaterial(uint materialSlot) {
    AGFXStructuredBuffer<GPUMaterial> bMaterials = AGFXStructuredBuffer<GPUMaterial>::Create(g_Constants.rMaterialBuffer);
    return bMaterials.Load(materialSlot);
}

#define SCENE_LOAD_SCHEME_PARAMS(type, materialSlot) \
    (AGFXStructuredBuffer<type>::Create(g_Constants.rSchemeParams).Load(materialSlot))

uint3 UnpackTriangle(AGFXByteAddressBuffer buf, uint byteOffset) {
    uint wordOffset = byteOffset & ~3u;
    uint shift = (byteOffset - wordOffset) * 8;
    uint2 words = buf.Load2(wordOffset);
    uint packed = (shift == 0) ? words.x : ((words.x >> shift) | (words.y << (32 - shift)));
    return uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
}

// One instance's meshlets, compacted down to the ones SceneAS decided to hand to the mesh shader.
// uMeshletIndices holds the *original* meshlet index (into rMeshletBuffer/rMeshletBoundsBuffer) for
// each compacted slot, since SV_GroupID.x in SceneMS is only the compacted position.
struct MeshletPayload {
    uint uInstanceIndex;
    uint uSelectedLod; // resolved once in SceneAS, so SceneMS doesn't need its own buffer read
    uint uMeshletIndices[kMeshletTaskGroupSize];
};

groupshared MeshletPayload s_Payload;

// Screen-space silhouette extents (left/right in xy, bottom/top in zw) of a view-space bounding
// sphere, as tangent-plane fractions -- multiply by 0.5 and add 0.5 to turn into UV space, or
// compare directly against NDC xy. `pos` must already be in view space (this only uses proj's
// diagonal scale terms, not a full view transform) and the camera must be outside the sphere
// (pos.x*pos.x + pos.z*pos.z must exceed radius*radius, i.e. never call this on a meshlet whose
// bounding sphere contains the camera). Ported as-is from a right-handed, camera-looks-down--Z
// source engine, matching proj._33==1/proj._43==0 (or proj._34==1/proj._44==0 if this matrix turns
// out to be row_major) -- double-check that against how frame.mProjection is actually laid out
// before trusting the left/right and bottom/top signs.
float4 SphereScreenExtents(float3 pos, float radius, float4x4 proj)
{
    float rad2 = radius * radius;
    float d = pos.z * radius;

    float hv = sqrt(pos.x * pos.x + pos.z * pos.z - rad2);
    float ha = pos.x * hv, hb = pos.x * radius, hc = pos.z * hv;

    float vv = sqrt(pos.y * pos.y + pos.z * pos.z - rad2);
    float va = pos.y * vv, vb = pos.y * radius, vc = pos.z * vv;

    float4 result;
    result.x = (ha - d) * proj._11 / (hc + hb); // left
    result.z = (ha + d) * proj._11 / (hc - hb); // right
    result.y = (va - d) * proj._22 / (vc + vb); // bottom
    result.w = (va + d) * proj._22 / (vc - vb); // top
    return result;
}

bool ContributionCullMeshlet(GPUInstance instance, MeshletCullData data, FrameConstants frame) {
    float3 center;
    float radius;
    SceneGetMeshletBoundingSphere(instance, data, center, radius);

    float4 lbrt = SphereScreenExtents(center, radius, frame.mProjection);
    float w = abs(lbrt.z - lbrt.x);
    float h = abs(lbrt.w - lbrt.y);

    const float minContribution = 0.001f;
    if (max(w, h) < minContribution)
        return false;
    return true;
}

bool FrustumCullMeshlet(GPUInstance instance, MeshletCullData data, FrameConstants frame) {
    float3 center;
    float radius;
    SceneGetMeshletBoundingSphere(instance, data, center, radius);

    [unroll]
    for (int plane = 0; plane < 6; ++plane) {
        float4 frustumPlane = frame.vFrustumPlanes[plane];
        float3 normal = frustumPlane.xyz;
        float distance = frustumPlane.w;

        if (dot(normal, center) + distance < -radius)
            return false;
    }
    return true;
}

bool ConeCullMeshlet(GPUInstance instance, MeshletCullData data, FrameConstants frame) {
    float3 apex;
    float3 axis;
    float cutoff;
    SceneGetMeshletCone(instance, data, apex, axis, cutoff);

    if (dot(normalize(apex - frame.vCameraPosition), axis) > cutoff)
        return false;
    return true;
}

[numthreads(kMeshletTaskGroupSize, 1, 1)]
void SceneAS(uint3 uGroupID : SV_GroupID, uint3 uGroupThreadID : SV_GroupThreadID) {
    uint instanceIndex = SceneResolveInstanceIndex();
    uint meshletIndex = uGroupID.x * kMeshletTaskGroupSize + uGroupThreadID.x;

    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants).Load(0);
    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(instanceIndex);

    uint selectedLod = AGFXStructuredBuffer<uint>::Create(g_Constants.rSelectedLodBuffer).Load(instanceIndex);
    GPULodInfo lodInfo = SceneLoadLodInfo(g_Constants.rInstanceLodTable, instanceIndex, selectedLod);
    MeshletCullData cull = SceneLoadMeshletBounds(lodInfo.rMeshletBoundsBuffer, meshletIndex);

    // Cone culling removes meshlets whose every triangle faces away from the camera -- only valid
    // for single-sided geometry. Double-sided materials (e.g. curtains) legitimately show their
    // back faces (all scene pipelines rasterize with CullMode::None), so skip the test for them.
    GPUMaterial material = SceneLoadMaterial(instance.uMaterialSlot);

    bool visible = meshletIndex < lodInfo.uMeshletCount;
    visible &= FrustumCullMeshlet(instance, cull, frame);
    visible &= GPUMaterialIsDoubleSided(material) || ConeCullMeshlet(instance, cull, frame);
    visible &= ContributionCullMeshlet(instance, cull, frame);

    // Compaction assumes the whole group executes as one SIMD wave (kMeshletTaskGroupSize == 32,
    // the wave width this engine targets) -- Wave*() intrinsics only see the calling lane's wave,
    // not the full thread group, so this silently drops meshlets if the group ever spans >1 wave.
    uint compactedIndex = WavePrefixCountBits(visible);
    if (visible)
        s_Payload.uMeshletIndices[compactedIndex] = meshletIndex;
    s_Payload.uInstanceIndex = instanceIndex;
    s_Payload.uSelectedLod = selectedLod;

    uint visibleCount = WaveActiveCountBits(visible);
    DispatchMesh(visibleCount, 1, 1, s_Payload);
}

[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void SceneMS(
    in payload MeshletPayload payload,
    uint3 uGroupID : SV_GroupID,
    uint3 uGroupThreadID : SV_GroupThreadID,
    out indices uint3 outTriangles[124],
    out vertices VSOut outVertices[64],
    out primitives PrimOut outPrims[124])
{
    uint instanceIndex = payload.uInstanceIndex;
    uint meshletIndex = payload.uMeshletIndices[uGroupID.x];

    AGFXStructuredBuffer<GPUInstance> bInstances = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer);
    GPUInstance instance = bInstances.Load(instanceIndex);

    GPULodInfo lodInfo = SceneLoadLodInfo(g_Constants.rInstanceLodTable, instanceIndex, payload.uSelectedLod);

    AGFXStructuredBuffer<MeshletDesc> bMeshlets = AGFXStructuredBuffer<MeshletDesc>::Create(lodInfo.rMeshletBuffer);
    MeshletDesc meshlet = bMeshlets.Load(meshletIndex);

    SetMeshOutputCounts(meshlet.uVertexCount, meshlet.uTriangleCount);

    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants).Load(0);
    float4x4 mViewProj = frame.mViewProjection;
    float4x4 mModel = instance.mTransform;

    AGFXStructuredBuffer<uint> bMeshletVertices = AGFXStructuredBuffer<uint>::Create(lodInfo.rMeshletVertexBuffer);
    AGFXStructuredBuffer<Vertex> bVertices = AGFXStructuredBuffer<Vertex>::Create(instance.rVertexBuffer);

    for (uint v = uGroupThreadID.x; v < meshlet.uVertexCount; v += 32) {
        uint vertexIndex = bMeshletVertices.Load(meshlet.uVertexOffset + v);
        Vertex vertex = bVertices.Load(vertexIndex);

        float4 worldPosition = mul(mModel, float4(vertex.vPosition, 1.0f));

        VSOut o;
        o.vPosition = mul(mViewProj, worldPosition);
        o.vWorldNormal = mul((float3x3)mModel, vertex.vNormal); // uniform-scale assumption, no inverse-transpose
        o.vUV = vertex.vUV;
        o.uMaterialSlot = instance.uMaterialSlot;
        o.vWorldPosition = worldPosition.xyz;
        o.vWorldTangent = float4(mul((float3x3)mModel, vertex.vTangent.xyz), vertex.vTangent.w);
        o.uMeshletID = meshletIndex;
        o.uInstanceIndex = instanceIndex;
        o.uLOD = payload.uSelectedLod;
        outVertices[v] = o;
    }

    AGFXByteAddressBuffer bTriangles = AGFXByteAddressBuffer::Create(lodInfo.rMeshletTriangleBuffer);
    for (uint t = uGroupThreadID.x; t < meshlet.uTriangleCount; t += 32) {
        outTriangles[t] = UnpackTriangle(bTriangles, meshlet.uTriangleOffset + t * 3);
        outPrims[t].uPrimitiveID = t;
    }
}

#endif
