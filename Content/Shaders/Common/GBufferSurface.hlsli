/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Decoding one gbuffer pixel into a shadeable surface, parameterised on explicit handles rather than
// on a push-constant block.
//
// Split out of DeferredShading.hlsli because a translation unit may only declare one push-constant
// block -- the same constraint that forced ClusterCull.hlsli to exist. Any shading path that is not
// the per-scheme material dispatch (ReSTIR, and whatever follows it) has its own constants and so
// cannot include DeferredShading.hlsli, but must decode the gbuffer identically or the two paths
// silently disagree about what they are shading.

#ifndef CARAMEL_GBUFFERSURFACE_HLSL
#define CARAMEL_GBUFFERSURFACE_HLSL

#include "AGFX.hlsli"
#include "SceneGeometry.hlsli"

// The gbuffer, decoded, plus the world position depth implies. What a shading kernel actually needs.
struct DeferredSurface {
    float3 vAlbedo;
    float3 vNormal;
    float3 vEmissive;
    float  fMetallic;
    float  fRoughness;
    float3 vWorldPosition;
    float3 vViewDirection;   // surface -> camera, normalized
    uint   uMaterialSlot;
};

// The handle set GBufferLoadSurface reads. A caller fills this from its own push constants; the
// field names deliberately match the push-constant fields on both sides so the mapping is a
// one-liner and a mismatch is visible at the call site.
struct GBufferHandles {
    ResourceHandle rFrameConstants;
    ResourceHandle rDepth;
    ResourceHandle rAlbedo;
    ResourceHandle rNormal;
    ResourceHandle rMetallicRoughness;
    ResourceHandle rEmissive;
};

DeferredSurface GBufferLoadSurface(GBufferHandles h, uint2 pixel, uint materialSlot, uint width, uint height) {
    DeferredSurface surface = (DeferredSurface)0;
    surface.uMaterialSlot = materialSlot;

    int2 coord = int2(pixel);
    surface.vAlbedo   = AGFXTexture2D<float4>::Create(h.rAlbedo).Load(coord).rgb;
    surface.vNormal   = normalize(AGFXTexture2D<float4>::Create(h.rNormal).Load(coord).xyz);
    surface.vEmissive = AGFXTexture2D<float4>::Create(h.rEmissive).Load(coord).rgb;

    float2 metallicRoughness = AGFXTexture2D<float4>::Create(h.rMetallicRoughness).Load(coord).rg;
    surface.fMetallic = metallicRoughness.x;
    surface.fRoughness = metallicRoughness.y;

    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(h.rFrameConstants).Load(0);
    float depth = AGFXTexture2D<float>::Create(h.rDepth).Load(coord);
    float2 uv = (float2(pixel) + 0.5f) / float2(width, height);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(frame.mInvViewProjection, clip);
    surface.vWorldPosition = world.xyz / world.w;
    surface.vViewDirection = normalize(frame.vCameraPosition - surface.vWorldPosition);

    return surface;
}

#endif
