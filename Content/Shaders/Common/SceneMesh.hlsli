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
#include "SceneGeometry.hlsli"
#include "HZB.hlsli"

#pragma task SceneAS
#pragma mesh SceneMS

AGFX_DECLARE_DRAW_ID();

struct ScenePushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceBuffer;
    ResourceHandle rMaterialBuffer;
    ResourceHandle rSchemeParams;
    ResourceHandle rDrawIndirection; // valid on every backend; only read on Vulkan
    ResourceHandle rSampler;
    ResourceHandle rFallbackTexture; // handle held by a material texture slot with nothing bound
    ResourceHandle rInstanceLodTable;  // GPULodInfo per (instance, lod) -- see GPUScene.hlsli
    uint uDrawIndirectionBase;         // start of this execute's bundle region in rDrawIndirection;
                                       // region * capacity, see SceneRenderer's region table. Vulkan only
    uint uCullFlags;                   // bit0 = culling enabled, bit1 = the pyramid holds real data
    ResourceHandle rHZB;
    // Two scalars, deliberately not a uint2. Push constants pack by cbuffer rules, where a uint2 may
    // not straddle a 16-byte boundary -- at this offset it would be bumped forward by one slot, and
    // every field after it would then read its neighbour's value (garbage HZB dimensions, and the
    // two visibility handles below shifted onto the wrong resources). Scalars cannot straddle, so
    // this stays correct no matter what is added or removed above it.
    uint uHZBWidth;
    uint uHZBHeight;
    uint uHZBMipCount;
    ResourceHandle rInstanceVisibility; // uint per *state slot*, layout below -- written by the populate CS
    ResourceHandle rMeshletVisibility;  // raw bitfield, uMeshletVisStride words per state slot
    uint uMeshletVisStride;
};
AGFX_PUSH_CONSTANTS(ScenePushConstants, g_Constants);

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
    float2 vUV : TEXCOORD0;
    nointerpolation uint uMaterialSlot : TEXCOORD1;
    nointerpolation uint uMeshletID : TEXCOORD2;
    nointerpolation uint uInstanceIndex : TEXCOORD3;
    nointerpolation uint uLOD : TEXCOORD4;
    nointerpolation uint uFade : TEXCOORD5; // fade nibble | outgoing << 4, see SceneLodDither
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

// 4x4 ordered Bayer dissolve: 16 thresholds map 1:1 onto the 16 fade levels, so coverage grows by
// exactly one pixel per 4x4 tile per level and every pixel flips exactly once over a fade. The
// incoming and outgoing halves partition the thresholds exactly (threshold < fade vs >= fade), so
// a cross-fading surface never shows holes or double-covered pixels. Replaced the previous
// screen-space white-noise hash, which read as crawling salt-and-pepper noise now that the resolve
// shades real materials instead of the flat normal debug view.
void SceneLodDither(VSOut input) {
    uint fade = input.uFade & 0xFu;
    bool outgoing = (input.uFade & 0x10u) != 0u;
    if (!outgoing && fade == kDrawWordFadeOpaque)
        return;
    static const uint bayer[16] = {
        0, 8, 2, 10,
        12, 4, 14, 6,
        3, 11, 1, 9,
        15, 7, 5, 13
    };
    uint2 p = (uint2)input.vPosition.xy & 3u;
    uint threshold = bayer[p.y * 4 + p.x];
    if (outgoing ? (threshold < fade) : (threshold >= fade))
        discard;
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

// One meshlet's cull data already transformed into world space. Every test below needs some part of
// this, and the sphere and the cone share the instance's basis lengths -- deriving it once per
// meshlet instead of once per test removes ~5 matrix transforms and ~12 length() per meshlet thread,
// which at these instance counts is the bulk of what SceneAS does. It also keeps far fewer values
// live across the test chain, which is what the occupancy counter is complaining about.
struct MeshletBoundsWS {
    float3 vCenter;
    float  fRadius;
    float3 vConeApex;
    float3 vConeAxis;
    float  fConeCutoff;
};

MeshletBoundsWS SceneTransformMeshletBounds(GPUInstance instance, MeshletCullData data) {
    float3x3 rs = (float3x3)instance.mTransform;
    float sx = length(mul(rs, float3(1.0, 0.0, 0.0)));
    float sy = length(mul(rs, float3(0.0, 1.0, 0.0)));
    float sz = length(mul(rs, float3(0.0, 0.0, 1.0)));
    float sMin = min(sx, min(sy, sz));
    float sMax = max(sx, max(sy, sz));

    MeshletBoundsWS bounds;
    bounds.vCenter = mul(instance.mTransform, float4(data.vCenter, 1.0)).xyz;
    bounds.fRadius = data.fRadius * sMax;
    bounds.vConeApex = mul(instance.mTransform, float4(data.vConeApex, 1.0)).xyz;
    bounds.vConeAxis = normalize(mul(rs, data.vConeAxis));
    // Same as SceneGetMeshletCone: non-uniform scale tilts the cone, so a cutoff of 1 disables the
    // test rather than culling geometry that is still facing the camera.
    bounds.fConeCutoff = sMax > sMin * 1.001f ? 1.0f : data.fConeCutoff;
    return bounds;
}

bool ContributionCullMeshlet(MeshletBoundsWS bounds, FrameConstants frame) {
    float3 viewCenter = mul(frame.mView, float4(bounds.vCenter, 1.0)).xyz;
    float rad2 = bounds.fRadius * bounds.fRadius;
    if (dot(viewCenter.xz, viewCenter.xz) <= rad2 || dot(viewCenter.yz, viewCenter.yz) <= rad2)
        return true;

    float4 lbrt = SphereScreenExtents(viewCenter, bounds.fRadius, frame.mProjection);
    float w = abs(lbrt.z - lbrt.x);
    float h = abs(lbrt.w - lbrt.y);

    const float minContribution = 0.001f;
    if (max(w, h) < minContribution)
        return false;
    return true;
}

bool FrustumCullMeshlet(MeshletBoundsWS bounds, FrameConstants frame) {
    [unroll]
    for (int plane = 0; plane < 6; ++plane) {
        float4 frustumPlane = frame.vFrustumPlanes[plane];
        float3 normal = frustumPlane.xyz;
        float distance = frustumPlane.w;

        if (dot(normal, bounds.vCenter) + distance < -bounds.fRadius)
            return false;
    }
    return true;
}

bool OcclusionCullMeshlet(MeshletBoundsWS bounds, FrameConstants frame) {
    const uint required = kSceneCullFlagHZBValid | kSceneCullFlagLatePass;
    if ((g_Constants.uCullFlags & required) != required)
        return true;

    HZBParams hzb = HZBMakeParams(g_Constants.rHZB, g_Constants.uHZBWidth, g_Constants.uHZBHeight, g_Constants.uHZBMipCount);
    return !HZBIsOccluded(bounds.vCenter - bounds.fRadius, bounds.vCenter + bounds.fRadius,
                          frame.mHZBViewProjection, hzb);
}

bool ConeCullMeshlet(MeshletBoundsWS bounds, FrameConstants frame) {
    if (dot(normalize(bounds.vConeApex - frame.vCameraPosition), bounds.vConeAxis) > bounds.fConeCutoff)
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
        // Chained through `if (visible)` rather than `&=`: `&=` evaluates both operands, so every
        // meshlet paid for the cone test's normalize and the contribution test's two sqrts even
        // after it had already been rejected. Ordered cheapest-first for the same reason.
        MeshletBoundsWS bounds = SceneTransformMeshletBounds(instance, cull);

        if (visible)
            visible = FrustumCullMeshlet(bounds, frame);
        if (visible && !GPUMaterialIsDoubleSided(material))
            visible = ConeCullMeshlet(bounds, frame);
        if (visible)
            visible = ContributionCullMeshlet(bounds, frame);

        // The outgoing half of a cross-fade deliberately skips the occlusion test, unlike every
        // other draw here. Two reasons, both specific to it:
        //
        // The pyramid it would be tested against was built from this frame's early-pass depth, which
        // during a fade holds the dithered *incoming* half of this same surface at a different LOD.
        // Occlusion culling assumes the pyramid contains occluders; here it contains the candidate
        // itself, one LOD removed, so the two halves of one surface compete and the simplified one
        // loses wherever the other sits in front of it.
        //
        // And it is the one draw with no recovery path. Anything the late pass wrongly culls is
        // normally either already on screen from the early pass or redrawn next frame once the
        // visibility bits rebuild -- but the outgoing half is late-pass only, never enters the
        // meshlet visibility bits, and its depth never reaches the pyramid (built before the late
        // pass). A single false positive deletes it outright and nothing corrects it. It lives for
        // under kLodFadeSeconds over a shrinking share of pixels, so frustum + cone + contribution
        // are enough.
        if (!SceneDrawWordOutgoing(drawWord)) {
            // Both of these persist across frames, so they are keyed by the instance's stable state
            // slot rather than its instance index -- see SceneStateSlot in GPUScene.hlsli. The
            // populate CS writes them under the same key.
            uint slot = SceneStateSlot(instance);
            uint instFlags = AGFXStructuredBuffer<uint>::Create(g_Constants.rInstanceVisibility).Load(slot);
            uint wordAddress = (slot * g_Constants.uMeshletVisStride + (meshletIndex >> 5)) * 4;
            uint mask = 1u << (meshletIndex & 31u);

            if ((g_Constants.uCullFlags & kSceneCullFlagLatePass) != 0) {
                if (visible)
                    visible = OcclusionCullMeshlet(bounds, frame);

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
        o.vUV = vertex.vUV;
        o.uMaterialSlot = instance.uMaterialSlot;
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
