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
#include "HZB.hlsli"

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
    float4x4 mHZBViewProjection; // the camera the current pyramid was rasterized from
};

struct ScenePushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceBuffer;
    ResourceHandle rMaterialBuffer;
    ResourceHandle rSchemeParams;
    ResourceHandle rDrawIndirection; // valid on every backend; only read on Vulkan
    ResourceHandle rSampler;
    ResourceHandle rFallbackTexture; // handle held by a material texture slot with nothing bound
    uint uDebugId; // scene.debug_id (see Core/CVar.hpp), read by DebugMeshletID.hlsl
    ResourceHandle rInstanceLodTable;  // GPULodInfo per (instance, lod) -- see GPUScene.hlsli
    uint uDrawIndirectionBase;         // start of this execute's bundle region in rDrawIndirection;
                                       // region * capacity, see SceneRenderer's region table. Vulkan only
    uint uCullFlags;                   // bit0 = culling enabled, bit1 = the pyramid holds real data
    ResourceHandle rHZB;
    uint2 uHZBSize;
    uint uHZBMipCount;
    ResourceHandle rInstanceVisibility; // uint per instance, layout below -- written by the populate CS
    ResourceHandle rMeshletVisibility;  // raw bitfield, uMeshletVisStride words per instance
    uint uMeshletVisStride;
};
AGFX_PUSH_CONSTANTS(ScenePushConstants, g_Constants);

static const uint kSceneDebugIdMeshlet = 0;
static const uint kSceneDebugIdPrimitive = 1;
static const uint kSceneDebugIdInstance = 2;
static const uint kSceneDebugIdLod = 3;
static const uint kSceneCullFlagEnabled = 1u;
static const uint kSceneCullFlagHZBValid = 2u;
static const uint kSceneCullFlagLatePass = 4u;
static const uint kSceneInstanceVisDrawnEarly = 2u;
static const uint kSceneInstanceVisBitsLodValid = 4u;

uint SceneResolveDrawWord() {
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
    nointerpolation uint uFade : TEXCOORD7; // fade nibble | outgoing << 4, see SceneLodDither
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

void SceneLodDither(VSOut input) {
    uint fade = input.uFade & 0xFu;
    bool outgoing = (input.uFade & 0x10u) != 0u;
    if (!outgoing && fade == kDrawWordFadeOpaque)
        return;
    float f = (float)fade / (float)kDrawWordFadeOpaque;
    float n = frac(52.9829189f * frac(dot(input.vPosition.xy, float2(0.06711056f, 0.00583715f))));
    if (outgoing ? (n < f) : (n >= f))
        discard;
}

uint3 UnpackTriangle(AGFXByteAddressBuffer buf, uint byteOffset) {
    uint wordOffset = byteOffset & ~3u;
    uint shift = (byteOffset - wordOffset) * 8;
    uint2 words = buf.Load2(wordOffset);
    uint packed = (shift == 0) ? words.x : ((words.x >> shift) | (words.y << (32 - shift)));
    return uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
}

struct MeshletPayload {
    uint uInstanceIndex;
    uint uDrawWord; // resolved once in SceneAS, so SceneMS doesn't need its own resolve
    uint uMeshletIndices[kMeshletTaskGroupSize];
};

groupshared MeshletPayload s_Payload;

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

    float3 viewCenter = mul(frame.mView, float4(center, 1.0)).xyz;
    float rad2 = radius * radius;
    if (dot(viewCenter.xz, viewCenter.xz) <= rad2 || dot(viewCenter.yz, viewCenter.yz) <= rad2)
        return true;

    float4 lbrt = SphereScreenExtents(viewCenter, radius, frame.mProjection);
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

bool OcclusionCullMeshlet(GPUInstance instance, MeshletCullData data, FrameConstants frame) {
    const uint required = kSceneCullFlagHZBValid | kSceneCullFlagLatePass;
    if ((g_Constants.uCullFlags & required) != required)
        return true;

    float3 center;
    float radius;
    SceneGetMeshletBoundingSphere(instance, data, center, radius);

    HZBParams hzb = HZBMakeParams(g_Constants.rHZB, g_Constants.uHZBSize.x, g_Constants.uHZBSize.y, g_Constants.uHZBMipCount);
    return !HZBIsOccluded(center - radius, center + radius, frame.mHZBViewProjection, hzb);
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
    uint drawWord = SceneResolveDrawWord();
    uint instanceIndex = SceneDrawWordInstance(drawWord);
    uint meshletIndex = uGroupID.x * kMeshletTaskGroupSize + uGroupThreadID.x;

    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants).Load(0);
    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(instanceIndex);

    uint selectedLod = SceneDrawWordLod(drawWord);
    GPULodInfo lodInfo = SceneLoadLodInfo(g_Constants.rInstanceLodTable, instanceIndex, selectedLod);
    MeshletCullData cull = SceneLoadMeshletBounds(lodInfo.rMeshletBoundsBuffer, meshletIndex);
    GPUMaterial material = SceneLoadMaterial(instance.uMaterialSlot);

    bool visible = meshletIndex < lodInfo.uMeshletCount;
    if ((g_Constants.uCullFlags & kSceneCullFlagEnabled) != 0) {
        visible &= FrustumCullMeshlet(instance, cull, frame);
        visible &= GPUMaterialIsDoubleSided(material) || ConeCullMeshlet(instance, cull, frame);
        visible &= ContributionCullMeshlet(instance, cull, frame);

        if (SceneDrawWordOutgoing(drawWord)) {
            visible &= OcclusionCullMeshlet(instance, cull, frame);
        } else {
            uint instFlags = AGFXStructuredBuffer<uint>::Create(g_Constants.rInstanceVisibility).Load(instanceIndex);
            uint wordAddress = (instanceIndex * g_Constants.uMeshletVisStride + (meshletIndex >> 5)) * 4;
            uint mask = 1u << (meshletIndex & 31u);

            if ((g_Constants.uCullFlags & kSceneCullFlagLatePass) != 0) {
                visible &= OcclusionCullMeshlet(instance, cull, frame);

                AGFXRWByteAddressBuffer bits = AGFXRWByteAddressBuffer::Create(g_Constants.rMeshletVisibility);
                uint previous;
                if (visible)
                    bits.InterlockedOr(wordAddress, mask, previous);
                else
                    bits.InterlockedAnd(wordAddress, ~mask, previous);

                bool drawnEarly = (instFlags & kSceneInstanceVisDrawnEarly) != 0
                               && ((instFlags & kSceneInstanceVisBitsLodValid) == 0 || (previous & mask) != 0);
                visible = visible && !drawnEarly;
            } else if ((instFlags & kSceneInstanceVisBitsLodValid) != 0) {
                AGFXByteAddressBuffer bits = AGFXByteAddressBuffer::Create(g_Constants.rMeshletVisibility);
                visible = visible && (bits.Load(wordAddress) & mask) != 0;
            }
        }
    }

    uint compactedIndex = WavePrefixCountBits(visible);
    if (visible)
        s_Payload.uMeshletIndices[compactedIndex] = meshletIndex;
    s_Payload.uInstanceIndex = instanceIndex;
    s_Payload.uDrawWord = drawWord;

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
    uint selectedLod = SceneDrawWordLod(payload.uDrawWord);
    uint fade = SceneDrawWordFade(payload.uDrawWord)
              | (SceneDrawWordOutgoing(payload.uDrawWord) ? 0x10u : 0u);

    AGFXStructuredBuffer<GPUInstance> bInstances = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer);
    GPUInstance instance = bInstances.Load(instanceIndex);

    GPULodInfo lodInfo = SceneLoadLodInfo(g_Constants.rInstanceLodTable, instanceIndex, selectedLod);

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
        o.vWorldNormal = vertex.vNormal;
        o.vUV = vertex.vUV;
        o.uMaterialSlot = instance.uMaterialSlot;
        o.vWorldPosition = worldPosition.xyz;
        o.vWorldTangent = float4(mul((float3x3)mModel, vertex.vTangent.xyz), vertex.vTangent.w);
        o.uMeshletID = meshletIndex;
        o.uInstanceIndex = instanceIndex;
        o.uLOD = selectedLod;
        o.uFade = fade;
        outVertices[v] = o;
    }

    AGFXByteAddressBuffer bTriangles = AGFXByteAddressBuffer::Create(lodInfo.rMeshletTriangleBuffer);
    for (uint t = uGroupThreadID.x; t < meshlet.uTriangleCount; t += 32) {
        outTriangles[t] = UnpackTriangle(bTriangles, meshlet.uTriangleOffset + t * 3);
        outPrims[t].uPrimitiveID = t;
    }
}

#endif
