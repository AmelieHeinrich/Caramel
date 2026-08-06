/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 10:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#ifndef CARAMEL_GPUSCENE_HLSL
#define CARAMEL_GPUSCENE_HLSL

#include "AGFX.hlsli"

// Indices into GPUMaterial::textures, mirroring MaterialTextureSlot (Sources/Caramel/Asset/Model.hpp).
static const uint kMaterialTextureBaseColor         = 0;
static const uint kMaterialTextureNormal            = 1;
static const uint kMaterialTextureMetallicRoughness = 2;
static const uint kMaterialTextureOcclusion         = 3;
static const uint kMaterialTextureEmissive          = 4;

// Candidate meshlets processed per amplification-shader (#pragma task) group -- shared between
// SceneMesh.hlsli's SceneAS and PopulateOpaqueIndirectBundle.hlsl, which must dispatch exactly
// ceil(instance.uMeshletCount / kMeshletTaskGroupSize) task groups per instance.
static const uint kMeshletTaskGroupSize = 32;

// Mirrors CaramelAsset::kLodCount (Sources/CaramelAsset/Format.hpp). Index 0 = coarsest, kLodCount-1
// = finest (see MeshCompressor::Compile).
static const uint kLodCount = 5;

// Mirrors GPUMaterial (Sources/Caramel/Scene/GPUScene.hpp) field-for-field. 80 bytes.
struct GPUMaterial {
    float4 vBaseColorFactor;
    float4 vEmissiveFactor;     // w unused
    float  fMetallicFactor;
    float  fRoughnessFactor;
    float  fAlphaCutoff;
    uint   uFlags;              // bit0 = doubleSided, bit1 = metallic overridden (ignore texture),
                                 // bit2 = roughness overridden (ignore texture),
                                 // bit3 = alphaTested (alphaMode "Mask"/"Blend" until a real
                                 // translucent pass exists)
    uint   uTextures[5];        // non-resident slots hold a 1x1 fallback handle, never invalid
    uint   uSchemeId;           // which material scheme drew this; debugging/validation only
    uint2  uPad;
};

// Mirrors ELightType (Sources/Caramel/Scene/SceneNode.hpp).
static const uint kLightTypeDirectional = 0;
static const uint kLightTypePoint       = 1;
static const uint kLightTypeSpot        = 2;
static const uint kLightTypeArea        = 3;

// Mirrors EAreaShape (Sources/Caramel/Scene/SceneNode.hpp), held in GPULight::uFlags bits 0-1.
static const uint kAreaShapeRect = 0;
static const uint kAreaShapeDisk = 1;
static const uint kAreaShapeTube = 2;
static const uint kAreaShapeMask = 3;
static const uint kLightFlagTwoSided = 4;

// Mirrors GPULight (Sources/Caramel/Scene/GPUScene.hpp) field-for-field. 96 bytes.
// One struct for all four types; a type leaves the fields it does not use zeroed.
struct GPULight {
    float4 vPositionRange;      // xyz world position, w range (0 = unbounded, i.e. directional)
    float4 vDirectionRadius;    // xyz direction the light points, w source radius
    float4 vColorIntensity;     // rgb colour, a intensity (exposed lux or candela)
    float4 vRightWidth;         // area: xyz right axis, w width (tube: length)
    float4 vUpHeight;           // area: xyz up axis, w height (tube/disk: radius)
    float  fSpotScale;          // cone falloff, saturate(cosAngle * scale + offset)
    float  fSpotOffset;         // non-spot lights carry scale 0 / offset 1, so this is always 1
    uint   uType;
    uint   uFlags;
};

// One light's contribution at a world position: the direction to sample the BRDF along, and the
// incident radiance along it. Returns false when the point is outside the light's range, so a caller
// can skip the BRDF entirely.
//
// Shared by every shading kernel rather than duplicated per scheme -- the placeholder sun this
// replaced was copy-pasted between DefaultPBR and Toon and had already drifted to two different
// directions.
bool LightEvaluate(GPULight light, float3 vWorldPosition, out float3 outL, out float3 outRadiance) {
    outL = float3(0.0f, 1.0f, 0.0f);
    outRadiance = 0.0f;

    if (light.uType == kLightTypeDirectional) {
        outL = -light.vDirectionRadius.xyz;
        outRadiance = light.vColorIntensity.rgb * light.vColorIntensity.a;
        return true;
    }

    float range = light.vPositionRange.w;
    float3 toLight = light.vPositionRange.xyz - vWorldPosition;
    float distSq = dot(toLight, toLight);
    if (distSq >= range * range)
        return false;

    outL = toLight * rsqrt(max(distSq, 1e-8f));

    // Inverse square, windowed so the falloff reaches exactly zero at the range instead of being
    // clipped there. That matters beyond looks: range is the bounding sphere clustered light culling
    // will test against (Notes/TODO.md), and a hard clip would make it a visible seam.
    float rangeSq = max(range * range, 1e-4f);
    float window = saturate(1.0f - (distSq * distSq) / (rangeSq * rangeSq));
    float attenuation = (window * window) / max(distSq, 1e-4f);

    // Non-spot lights carry fSpotScale 0 / fSpotOffset 1, so this is 1 for them -- cheaper than
    // branching on the type a second time.
    float cosAngle = dot(-outL, light.vDirectionRadius.xyz);
    float cone = saturate(cosAngle * light.fSpotScale + light.fSpotOffset);
    attenuation *= cone * cone;

    // Area lights are punctual here. Their nits were already integrated over the emissive area on the
    // CPU, so this is the correct far-field limit; LTC (Notes/TODO.md) replaces this branch and reads
    // vRightWidth/vUpHeight/uFlags, which is why the basis is already on the struct.
    outRadiance = light.vColorIntensity.rgb * (light.vColorIntensity.a * attenuation);
    return true;
}

// Mirrors GPUInstance (Sources/Caramel/Scene/GPUScene.hpp) field-for-field. 208 bytes.
// Bounds are float4 rather than float3 to keep the C++/HLSL layouts trivially identical.
struct GPUInstance {
    float4x4 mTransform;
    float4   vBoundsMin;        // w unused
    float4   vBoundsMax;        // w unused
    uint     uMaterialSlot;
    uint     rVertexBuffer;     // handles for the instance's *resident* LOD
    uint     rMeshletBuffer;
    uint     rMeshletVertexBuffer;
    uint     rMeshletTriangleBuffer;
    uint     uMeshletCount;
    uint     uLod;
    uint     rMeshletBoundsBuffer;
    float4x4 mPrevTransform;    // last frame's transform, for motion vectors
    uint     uStateSlot;        // index into the persistent per-instance state buffers -- NOT the
                                // instance index, which changes meaning across frames (see below)
    uint     uStateFresh;       // 1 = the slot was just handed out, its contents are someone else's
    float    fBoundsScaleMax;   // longest and shortest transformed basis axis of mTransform, baked
    float    fBoundsScaleMin;   // CPU-side so meshlet culling does not redo it per meshlet
};

// The instance buffer is compacted every frame: an instance whose model has no resident LOD is
// dropped, so every later instance shifts down. Anything that persists across frames and is keyed
// per instance -- the LOD cross-fade state, the instance visibility flags, the meshlet visibility
// bitfield -- must therefore be indexed by uStateSlot, which GPUScene keeps stable across
// compaction, never by the instance index. Indexing those by instance index makes state migrate to
// a different object whenever streaming changes residency: LOD fades retrigger on their own and the
// two-pass occlusion scheme replays the wrong meshlet set.
uint SceneStateSlot(GPUInstance instance) { return instance.uStateSlot; }
bool SceneStateIsFresh(GPUInstance instance) { return instance.uStateFresh != 0u; }

// Mirrors CaramelAsset::MeshletCullData (Sources/CaramelAsset/Format.hpp) field-for-field. One
// entry per meshlet in instance.rMeshletBoundsBuffer, indexed by meshlet index (0..uMeshletCount-1),
// same as rMeshletBuffer. Baked offline by meshopt_computeMeshletBounds -- vCenter/vConeApex/
// vConeAxis are in the mesh's local space, so transform by the instance's mTransform (uniform
// scale assumed, same as the mesh shader's normal transform) before testing.
struct MeshletCullData {
    float3 vCenter;
    float  fRadius;
    float3 vConeApex;
    float3 vConeAxis;
    float  fConeCutoff;
};

// Mirrors GPULodInfo (Sources/Caramel/Scene/GPUScene.hpp) field-for-field. 20 bytes, tight-packed
// (structured buffers here use natural packing, not cbuffer/push-constant alignment rules). One
// entry per (instance, lod) pair in the instance LOD table, indexed instanceIndex * kLodCount + lod.
// Handles are always valid for lod 0..kLodCount-1 (GPUModel creates GPU buffers for every LOD up
// front) -- but the *data* behind a handle is only meaningful if lod <= that instance's residency
// ceiling (GPUInstance::uLod). uMeshletCount is CPU-known statically for every LOD regardless of
// residency.
struct GPULodInfo {
    uint rMeshletBuffer;
    uint rMeshletVertexBuffer;
    uint rMeshletTriangleBuffer;
    uint rMeshletBoundsBuffer;
    uint uMeshletCount;
};

// Per-draw word carried through the drawId channel (D3D12/Metal: the drawId root constant, Vulkan:
// the draw-indirection buffer). Packs everything a draw needs that can differ between the two draws
// of a cross-fading instance. Fade 15 = fully opaque, the dither keeps every pixel.
static const uint kDrawWordInstanceMask = 0xFFFFFFu;
static const uint kDrawWordLodShift = 24;
static const uint kDrawWordOutgoingBit = 1u << 27;
static const uint kDrawWordFadeShift = 28;
static const uint kDrawWordFadeOpaque = 15u;

uint SceneMakeDrawWord(uint instanceIndex, uint lod, uint fade, bool outgoing) {
    return (instanceIndex & kDrawWordInstanceMask)
         | (lod << kDrawWordLodShift)
         | (outgoing ? kDrawWordOutgoingBit : 0u)
         | (fade << kDrawWordFadeShift);
}
uint SceneDrawWordInstance(uint word) { return word & kDrawWordInstanceMask; }
uint SceneDrawWordLod(uint word) { return (word >> kDrawWordLodShift) & 0x7u; }
bool SceneDrawWordOutgoing(uint word) { return (word & kDrawWordOutgoingBit) != 0u; }
uint SceneDrawWordFade(uint word) { return word >> kDrawWordFadeShift; }

bool GPUMaterialIsDoubleSided(GPUMaterial material) { return (material.uFlags & 1u) != 0u; }
bool GPUMaterialOverridesMetallic(GPUMaterial material) { return (material.uFlags & 2u) != 0u; }
bool GPUMaterialOverridesRoughness(GPUMaterial material) { return (material.uFlags & 4u) != 0u; }
bool GPUMaterialIsAlphaTested(GPUMaterial material) { return (material.uFlags & 8u) != 0u; }

MeshletCullData SceneLoadMeshletBounds(uint meshletBoundsBuffer, uint meshletIndex) {
    return AGFXStructuredBuffer<MeshletCullData>::Create(meshletBoundsBuffer).Load(meshletIndex);
}

// Resolves one instance's chosen-LOD buffer handles/count from the per-instance LOD table.
GPULodInfo SceneLoadLodInfo(uint instanceLodTable, uint instanceIndex, uint lod) {
    return AGFXStructuredBuffer<GPULodInfo>::Create(instanceLodTable).Load(instanceIndex * kLodCount + lod);
}

// World-space bounding sphere for one meshlet. Radius is scaled by the longest transformed basis
// axis, so non-uniform scale stays conservative (too loose along squashed axes, never too tight).
void SceneGetMeshletBoundingSphere(GPUInstance instance, MeshletCullData bounds, out float3 outCenter, out float outRadius) {
    outCenter = mul(instance.mTransform, float4(bounds.vCenter, 1.0)).xyz;
    float3x3 rs = (float3x3)instance.mTransform;
    float sx = length(mul(rs, float3(1.0, 0.0, 0.0)));
    float sy = length(mul(rs, float3(0.0, 1.0, 0.0)));
    float sz = length(mul(rs, float3(0.0, 0.0, 1.0)));
    outRadius = bounds.fRadius * max(sx, max(sy, sz));
}

void SceneGetMeshletCone(GPUInstance instance, MeshletCullData bounds, out float3 outApex, out float3 outAxis, out float outCutoff) {
    outApex = mul(instance.mTransform, float4(bounds.vConeApex, 1.0)).xyz;
    float3x3 rs = (float3x3)instance.mTransform;
    outAxis = normalize(mul(rs, bounds.vConeAxis));
    float sx = length(mul(rs, float3(1.0, 0.0, 0.0)));
    float sy = length(mul(rs, float3(0.0, 1.0, 0.0)));
    float sz = length(mul(rs, float3(0.0, 0.0, 1.0)));
    float sMin = min(sx, min(sy, sz));
    float sMax = max(sx, max(sy, sz));
    outCutoff = sMax > sMin * 1.001f ? 1.0f : bounds.fConeCutoff;
}

#endif
