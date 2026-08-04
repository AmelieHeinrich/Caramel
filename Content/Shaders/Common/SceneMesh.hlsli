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
    float4x4 mViewProj;
    float3   vCameraPosition;
    float    _Pad;
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
};
AGFX_PUSH_CONSTANTS(ScenePushConstants, g_Constants);

// AGFX_DRAW_ID() is the value the populate compute shader wrote as drawId on D3D12/Metal, but on
// Vulkan it is the linear position within the compacted indirect bundle instead (SKILL.md gotcha
// 6), so it must be resolved through the indirection buffer the populate shader wrote alongside it.
uint SceneResolveInstanceIndex() {
#if defined(AGFX_VULKAN)
    return AGFXByteAddressBuffer::Create(g_Constants.rDrawIndirection).Load(AGFX_DRAW_ID() * 4);
#else
    return AGFX_DRAW_ID();
#endif
}

struct VSOut {
    float4 vPosition : SV_POSITION;
    float3 vWorldNormal : NORMAL0;
    float2 vUV : TEXCOORD0;
    // The instance is read in the mesh shader, so the material index has to be forwarded. It is
    // constant across the meshlet -- nointerpolation keeps it exact.
    nointerpolation uint uMaterialSlot : TEXCOORD1;
    float3 vWorldPosition : TEXCOORD2;
    float4 vWorldTangent : TEXCOORD3; // xyz = world-space tangent, w = handedness (passthrough)
    nointerpolation uint uMeshletID : TEXCOORD4; // index into the instance's meshlet buffer (SV_GroupID.x)
    nointerpolation uint uInstanceIndex : TEXCOORD5; // resolved via SceneResolveInstanceIndex, forwarded because AGFX_DRAW_ID() is only valid in vertex/mesh/task stages on Vulkan
};

// Loads the material of the pixel being shaded. Every scheme's pixel shader starts with this.
GPUMaterial SceneLoadMaterial(uint materialSlot) {
    AGFXStructuredBuffer<GPUMaterial> bMaterials = AGFXStructuredBuffer<GPUMaterial>::Create(g_Constants.rMaterialBuffer);
    return bMaterials.Load(materialSlot);
}

// Scheme parameter buffers are indexed by global material slot, so no separate param index is
// needed -- see GPUScene.cpp for why that sparseness is deliberate.
#define SCENE_LOAD_SCHEME_PARAMS(type, materialSlot) \
    (AGFXStructuredBuffer<type>::Create(g_Constants.rSchemeParams).Load(materialSlot))

// meshoptimizer packs meshlet-local triangle indices as 3 consecutive bytes per triangle (no
// 4-byte alignment between triangles, only meshlet.uTriangleOffset itself is 4-byte aligned), so a
// triangle's 3 bytes can straddle a 4-byte boundary. Load the 8-byte window starting at the
// containing aligned word and shift/mask out the 3 bytes we actually want.
uint3 UnpackTriangle(AGFXByteAddressBuffer buf, uint byteOffset) {
    uint wordOffset = byteOffset & ~3u;
    uint shift = (byteOffset - wordOffset) * 8;
    uint2 words = buf.Load2(wordOffset);
    uint packed = (shift == 0) ? words.x : ((words.x >> shift) | (words.y << (32 - shift)));
    return uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
}

// kMeshletMaxVertices / kMeshletMaxTriangles from Sources/CaramelAsset/Format.hpp.
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void SceneMS(
    uint3 uGroupID : SV_GroupID,
    uint3 uGroupThreadID : SV_GroupThreadID,
    out indices uint3 outTriangles[124],
    out vertices VSOut outVertices[64])
{
    uint instanceIndex = SceneResolveInstanceIndex();

    AGFXStructuredBuffer<GPUInstance> bInstances = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer);
    GPUInstance instance = bInstances.Load(instanceIndex);

    AGFXStructuredBuffer<MeshletDesc> bMeshlets = AGFXStructuredBuffer<MeshletDesc>::Create(instance.rMeshletBuffer);
    MeshletDesc meshlet = bMeshlets.Load(uGroupID.x);

    SetMeshOutputCounts(meshlet.uVertexCount, meshlet.uTriangleCount);

    AGFXStructuredBuffer<FrameConstants> bFrame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants);
    float4x4 mViewProj = bFrame.Load(0).mViewProj;
    float4x4 mModel = instance.mTransform;

    AGFXStructuredBuffer<uint> bMeshletVertices = AGFXStructuredBuffer<uint>::Create(instance.rMeshletVertexBuffer);
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
        o.uMeshletID = uGroupID.x;
        o.uInstanceIndex = instanceIndex;
        outVertices[v] = o;
    }

    AGFXByteAddressBuffer bTriangles = AGFXByteAddressBuffer::Create(instance.rMeshletTriangleBuffer);
    for (uint t = uGroupThreadID.x; t < meshlet.uTriangleCount; t += 32) {
        outTriangles[t] = UnpackTriangle(bTriangles, meshlet.uTriangleOffset + t * 3);
    }
}

#endif
