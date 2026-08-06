/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 15:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Shared half of the deferred material path: the classification pass and every material scheme's
// shading kernel speak this file.
//
// A frame goes GBuffer Resolve -> Material Classify -> Material Shade. Classify walks the visibility
// buffer, bins every covered pixel by its material's scheme id, and writes one indirect dispatch
// command per scheme. Shade then replays those commands, one dispatch per scheme, so a scheme's
// kernel only ever runs on the pixels that actually belong to it -- no per-pixel branch over every
// shading model, and no pipeline permutations.

#ifndef CARAMEL_DEFERREDSHADING_HLSL
#define CARAMEL_DEFERREDSHADING_HLSL

#include "AGFX.hlsli"
#include "GPUScene.hlsli"
#include "SceneGeometry.hlsli"
#include "VisibilityBuffer.hlsli"

// Mirrors SchemeRegistry::kMaxSchemes (Sources/Caramel/Renderer/MaterialScheme.hpp). Fixed rather
// than dynamic because the classify shaders size groupshared arrays from it, and because it bounds
// the indirect bundle's region count.
static const uint kMaxShadingSchemes = 8;

// The classify buffer is three parallel kMaxShadingSchemes-sized uint arrays in one allocation:
// counts (how many pixels this scheme owns), offsets (its base in the pixel list, an exclusive
// prefix sum of counts), and cursors (the scatter pass's running allocation within its run).
//
// Byte offsets, and the buffer is a raw view, because only AGFXRWByteAddressBuffer carries the
// Interlocked* family -- AGFXRWStructuredBuffer has none. The pixel list is raw for the same reason
// of consistency, though only the scatter pass writes it.
static const uint kClassifyCountBase  = 0;
static const uint kClassifyOffsetBase = kMaxShadingSchemes * 4;
static const uint kClassifyCursorBase = kMaxShadingSchemes * 8;
static const uint kClassifySlotCount  = kMaxShadingSchemes * 3;

// Threads per group in a scheme's shading kernel. MaterialClassifyArgs divides each scheme's pixel
// count by this to get its dispatch size, so the two must agree.
static const uint kShadeGroupSize = 64;

// Tile the count and scatter passes walk the screen in. Both accumulate into groupshared counters
// and hit the global atomics once per (tile, scheme) instead of once per pixel -- with only a
// handful of schemes in a scene, a per-pixel global atomic is almost entirely contention.
static const uint kClassifyTileSize = 8;

// A pixel coordinate packed into the list. 16 bits per axis caps the viewport at 65535, which
// SceneRenderer asserts.
uint DeferredPackPixel(uint2 pixel) { return (pixel.y << 16) | pixel.x; }
uint2 DeferredUnpackPixel(uint packed) { return uint2(packed & 0xFFFFu, packed >> 16); }

// Everything the classify passes and the shading kernels agree on. Deliberately all scalars: push
// constants pack by cbuffer rules, where a uint2 may not straddle a 16-byte boundary, so inserting a
// field ahead of one silently shifts every field after it (see the same warning in SceneMesh.hlsli).
// Mirrors DeferredPushConstants in Sources/Caramel/Renderer/SceneRenderer.cpp. 84 bytes, well under
// the 128-byte root-signature ceiling.
struct DeferredPushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceBuffer;
    ResourceHandle rMaterialBuffer;
    ResourceHandle rSchemeParams;      // 0 when this scheme declares no parameters
    ResourceHandle rVisibility;
    ResourceHandle rDepth;
    ResourceHandle rAlbedo;
    ResourceHandle rNormal;
    ResourceHandle rMetallicRoughness;
    ResourceHandle rEmissive;
    ResourceHandle rPixelList;         // uint per covered pixel, grouped by scheme
    ResourceHandle rClassifyBuffer;    // counts / offsets / cursors, see kClassify*Base above
    ResourceHandle rOutput;            // RWTexture2D<float4> over the scene lighting buffer
    uint uSchemeId;                    // which scheme this dispatch shades; unused by classify
    uint uWidth;
    uint uHeight;
    uint uSchemeCount;
    // agfxIndirectBundleGetHandle() packs commands buffer in the low 32 bits and count buffer in the
    // high 32. Split into two uints rather than declared uint64_t: a uint64_t in a push-constant
    // struct pulls in 8-byte cbuffer alignment, so the C++ mirror needs padding the compiler inserts
    // silently and nothing catches when it drifts. Only MaterialClassifyArgs reads these.
    uint uBundleHandleLo;
    uint uBundleHandleHi;
    ResourceHandle rLightBuffer;       // stale or null when uLightCount is 0 -- never read unguarded
    uint uLightCount;
};
AGFX_PUSH_CONSTANTS(DeferredPushConstants, g_Constants);

// The scene's lights. uLightCount is the only thing keeping this safe: GPUScene's light buffer keeps
// its previous allocation on a frame with no lights, so the handle can outlive the lights it
// described. Loop to uLightCount and the body is simply unreachable.
GPULight DeferredLoadLight(uint index) {
    return AGFXStructuredBuffer<GPULight>::Create(g_Constants.rLightBuffer).Load(index);
}

uint64_t DeferredBundleHandle() {
    return ((uint64_t)g_Constants.uBundleHandleHi << 32) | (uint64_t)g_Constants.uBundleHandleLo;
}

// Loads one material slot's parameter block for the scheme being shaded. `type` is a struct the
// scheme's shader declares by hand: one float4 (or int4) per entry in that scheme's JSON
// "parameters" array, in the same order, with scalars read out of .x. A full slot each is what makes
// the layout unambiguous -- see MaterialScheme::ComputeLayout. Reorder the members and every field
// reads its neighbour's value.
#define DEFERRED_LOAD_SCHEME_PARAMS(type, materialSlot) \
    (AGFXStructuredBuffer<type>::Create(g_Constants.rSchemeParams).Load(materialSlot))

// The scheme a covered pixel belongs to, or false if the pixel is background. Shared so the count
// and scatter passes can never disagree about which pixels they bin -- a mismatch there overflows
// one scheme's run into the next.
bool DeferredClassifyPixel(uint2 pixel, out uint outSchemeId, out uint outMaterialSlot) {
    outSchemeId = 0;
    outMaterialSlot = 0;

    if (pixel.x >= g_Constants.uWidth || pixel.y >= g_Constants.uHeight)
        return false;

    // Background is depth == 1, never a uint sentinel: the visibility buffer clears to all-zero
    // because zero is the only portable uint clear value.
    if (AGFXTexture2D<float>::Create(g_Constants.rDepth).Load(int2(pixel)) >= 1.0f)
        return false;

    uint drawWord = AGFXTexture2D<uint2>::Create(g_Constants.rVisibility).Load(int2(pixel)).x;
    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(SceneDrawWordInstance(drawWord));
    GPUMaterial material = AGFXStructuredBuffer<GPUMaterial>::Create(g_Constants.rMaterialBuffer).Load(instance.uMaterialSlot);

    outMaterialSlot = instance.uMaterialSlot;
    // A material referencing a scheme that failed to load would otherwise scribble outside the
    // classify buffer; fall back to the default scheme, which always exists.
    outSchemeId = material.uSchemeId < g_Constants.uSchemeCount ? material.uSchemeId : 0;
    return true;
}

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

DeferredSurface DeferredLoadSurface(uint2 pixel, uint materialSlot) {
    DeferredSurface surface = (DeferredSurface)0;
    surface.uMaterialSlot = materialSlot;

    int2 coord = int2(pixel);
    surface.vAlbedo   = AGFXTexture2D<float4>::Create(g_Constants.rAlbedo).Load(coord).rgb;
    surface.vNormal   = normalize(AGFXTexture2D<float4>::Create(g_Constants.rNormal).Load(coord).xyz);
    surface.vEmissive = AGFXTexture2D<float4>::Create(g_Constants.rEmissive).Load(coord).rgb;

    float2 metallicRoughness = AGFXTexture2D<float4>::Create(g_Constants.rMetallicRoughness).Load(coord).rg;
    surface.fMetallic = metallicRoughness.x;
    surface.fRoughness = metallicRoughness.y;

    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants).Load(0);
    float depth = AGFXTexture2D<float>::Create(g_Constants.rDepth).Load(coord);
    float2 uv = (float2(pixel) + 0.5f) / float2(g_Constants.uWidth, g_Constants.uHeight);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(frame.mInvViewProjection, clip);
    surface.vWorldPosition = world.xyz / world.w;
    surface.vViewDirection = normalize(frame.vCameraPosition - surface.vWorldPosition);

    return surface;
}

// Resolves a shading thread's pixel out of its scheme's run in the pixel list. Returns false for the
// tail threads of the last group, which the dispatch size rounds up past the real count.
bool DeferredResolveThread(uint threadIndex, out uint2 outPixel, out uint outMaterialSlot) {
    outPixel = uint2(0, 0);
    outMaterialSlot = 0;

    AGFXByteAddressBuffer bClassify = AGFXByteAddressBuffer::Create(g_Constants.rClassifyBuffer);
    if (threadIndex >= bClassify.Load(kClassifyCountBase + g_Constants.uSchemeId * 4))
        return false;

    uint base = bClassify.Load(kClassifyOffsetBase + g_Constants.uSchemeId * 4);
    outPixel = DeferredUnpackPixel(AGFXByteAddressBuffer::Create(g_Constants.rPixelList).Load((base + threadIndex) * 4));

    // Cheaper than widening the pixel list to 8 bytes per pixel: two dependent loads against buffers
    // this thread's neighbours are hitting anyway.
    uint drawWord = AGFXTexture2D<uint2>::Create(g_Constants.rVisibility).Load(int2(outPixel)).x;
    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(SceneDrawWordInstance(drawWord));
    outMaterialSlot = instance.uMaterialSlot;
    return true;
}

void DeferredWrite(uint2 pixel, float3 color) {
    AGFXRWTexture2D<float4>::Create(g_Constants.rOutput).Store(int2(pixel), float4(color, 1.0f));
}

#endif
