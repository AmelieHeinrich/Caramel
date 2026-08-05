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
                                 // bit2 = roughness overridden (ignore texture)
    uint   uTextures[5];        // non-resident slots hold a 1x1 fallback handle, never invalid
    uint   uSchemeId;           // which material scheme drew this; debugging/validation only
    uint2  uPad;
};

// Mirrors GPUInstance (Sources/Caramel/Scene/GPUScene.hpp) field-for-field. 128 bytes.
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
};

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

bool GPUMaterialIsDoubleSided(GPUMaterial material) { return (material.uFlags & 1u) != 0u; }
bool GPUMaterialOverridesMetallic(GPUMaterial material) { return (material.uFlags & 2u) != 0u; }
bool GPUMaterialOverridesRoughness(GPUMaterial material) { return (material.uFlags & 4u) != 0u; }

MeshletCullData SceneLoadMeshletBounds(uint meshletBoundsBuffer, uint meshletIndex) {
    return AGFXStructuredBuffer<MeshletCullData>::Create(meshletBoundsBuffer).Load(meshletIndex);
}

// Resolves one instance's chosen-LOD buffer handles/count from the per-instance LOD table.
GPULodInfo SceneLoadLodInfo(uint instanceLodTable, uint instanceIndex, uint lod) {
    return AGFXStructuredBuffer<GPULodInfo>::Create(instanceLodTable).Load(instanceIndex * kLodCount + lod);
}

// World-space bounding sphere for one meshlet. Uniform-scale assumption: radius is scaled by the
// length of the transformed local +X axis, so a non-uniformly scaled instance reports a sphere
// that is too tight along its squashed axes and too loose along its stretched ones.
void SceneGetMeshletBoundingSphere(GPUInstance instance, MeshletCullData bounds, out float3 outCenter, out float outRadius) {
    outCenter = mul(instance.mTransform, float4(bounds.vCenter, 1.0)).xyz;
    float scale = length(mul((float3x3)instance.mTransform, float3(1.0, 0.0, 0.0)));
    outRadius = bounds.fRadius * scale;
}

// World-space normal cone for one meshlet: every triangle in the meshlet faces within fConeCutoff
// (a cosine of the half-angle, [-1, 1]) of vConeAxis around vConeApex, so a viewer outside that
// cone sees the whole meshlet back-facing and it can be culled. fConeCutoff == 1 means meshopt
// could not build a useful cone (e.g. curved/double-sided geometry) -- never cull on it.
// vConeAxis uses the same uniform-scale, no-inverse-transpose assumption as vWorldNormal in
// SceneMesh.hlsli's SceneMS.
void SceneGetMeshletCone(GPUInstance instance, MeshletCullData bounds, out float3 outApex, out float3 outAxis, out float outCutoff) {
    outApex = mul(instance.mTransform, float4(bounds.vConeApex, 1.0)).xyz;
    outAxis = normalize(mul((float3x3)instance.mTransform, bounds.vConeAxis));
    outCutoff = bounds.fConeCutoff;
}

#endif
