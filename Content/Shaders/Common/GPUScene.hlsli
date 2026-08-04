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

// Mirrors GPUMaterial (Sources/Caramel/Scene/GPUScene.hpp) field-for-field. 80 bytes.
struct GPUMaterial {
    float4 vBaseColorFactor;
    float4 vEmissiveFactor;     // w unused
    float  fMetallicFactor;
    float  fRoughnessFactor;
    float  fAlphaCutoff;
    uint   uFlags;              // bit0 = doubleSided
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
    uint     uPad;
};

bool GPUMaterialIsDoubleSided(GPUMaterial material) { return (material.uFlags & 1u) != 0u; }

#endif
