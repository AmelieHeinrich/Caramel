/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-07 14:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Shared half of the ReSTIR direct-lighting path: what the four stage kernels speak.
//
// A frame goes GBuffer Resolve -> Initial Sample -> Temporal Reuse -> Spatial Reuse -> Shade, one
// thread per pixel throughout. Unlike the scheme path there is no classification and no compacted
// pixel list: the reuse stages need the 2D neighbourhood, which compaction destroys, so every stage
// runs over the full screen and rejects background itself.
//
// !! STAGES 1-3 ARE SCAFFOLDING. No reservoir buffer is allocated yet -- rReservoirsIn and
// !! rReservoirsOut are 0 and must never be read unguarded. Stage 4 carries a real brute-force loop
// !! so that toggling scene.restir on produces a correct image today, which is the reference the
// !! reuse math gets diffed against as it lands.

#ifndef CARAMEL_RESTIR_HLSL
#define CARAMEL_RESTIR_HLSL

#include "AGFX.hlsli"
#include "GPUScene.hlsli"
#include "SceneGeometry.hlsli"
#include "VisibilityBuffer.hlsli"
#include "GBufferSurface.hlsli"
#include "LightList.hlsli"

// Threads per group per axis. Mirrors kReSTIRGroupSize in ReSTIRPass.cpp, which sizes the dispatch.
static const uint kReSTIRGroupSize = 8;

// Mirrors ReSTIRPushConstants in Sources/Caramel/Renderer/Passes/ReSTIRPass.cpp. All scalars,
// deliberately -- push constants pack by cbuffer rules, so a vector field can be bumped past a
// 16-byte boundary and silently shift everything after it (same warning as DeferredShading.hlsli).
struct ReSTIRPushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceBuffer;
    ResourceHandle rMaterialBuffer;

    ResourceHandle rVisibility;
    ResourceHandle rDepth;
    ResourceHandle rAlbedo;
    ResourceHandle rNormal;
    ResourceHandle rMetallicRoughness;
    ResourceHandle rEmissive;
    ResourceHandle rMotion;            // RG16F screen-space motion, what temporal reuse reprojects through

    ResourceHandle rOutput;            // RWTexture2D<float4> over the scene lighting buffer

    ResourceHandle rLightBuffer;       // stale or null when uLightCount is 0 -- never read unguarded
    uint           uLightCount;

    // The clustered light grid ClusteredLightPass fills, the candidate set initial sampling draws
    // from. See ClusteredLights.hlsli for the layouts.
    ResourceHandle rClusterLights;
    ResourceHandle rLightCull;
    float          fClusterSliceScale;
    float          fClusterSliceBias;
    uint           uClusteringEnabled; // 0 while scene.clustered_lights is off -- see LightListBegin

    // Both 0 until the reservoir buffers exist. Guard on them the way the shading kernels guard on
    // uClusteringEnabled: a zero handle describes nothing, it is not a valid empty buffer.
    ResourceHandle rReservoirsIn;
    ResourceHandle rReservoirsOut;

    uint  uWidth;
    uint  uHeight;
    uint  uFrameNumber;                // monotonic, not the frame-in-flight slot -- RNG seed
    float fAmbient;                    // scene.restir_ambient; the scheme path reads this per-material
};
AGFX_PUSH_CONSTANTS(ReSTIRPushConstants, g_Constants);

// One pixel's reservoir, as ReSTIR DI needs it. Sketched rather than declared because the layout
// decision -- how the sample is identified, whether the target function is cached, whether M is
// capped in the struct or at read time -- belongs to whoever fills the stages in, and ReSTIRPass
// allocates nothing yet.
//
// struct Reservoir {
//     uint  uSampleIndex;   // index into the scene light buffer, or ~0u for "empty"
//     float fWeightSum;     // running sum of candidate weights, the RIS denominator
//     uint  uM;             // candidates seen, capped so temporal reuse cannot run away
//     float fW;             // the unbiased contribution weight the shade stage multiplies by
// };
// static const uint kReservoirStride = 16;
// uint ReSTIRReservoirOffset(uint2 pixel) { return (pixel.y * g_Constants.uWidth + pixel.x) * kReservoirStride; }

bool ReSTIRHasReservoirs() {
    return g_Constants.rReservoirsIn != 0 && g_Constants.rReservoirsOut != 0;
}

// The covered pixel's material slot, or false if the pixel is background. Lifted from
// DeferredClassifyPixel minus the scheme lookup, which this path has no use for. Both must agree on
// what counts as covered, or ReSTIR shades a different set of pixels than the scheme path does.
bool ReSTIRResolvePixel(uint2 pixel, out uint outMaterialSlot) {
    outMaterialSlot = 0;

    if (pixel.x >= g_Constants.uWidth || pixel.y >= g_Constants.uHeight)
        return false;

    // Background is depth == 1, never a uint sentinel: the visibility buffer clears to all-zero
    // because zero is the only portable uint clear value.
    if (AGFXTexture2D<float>::Create(g_Constants.rDepth).Load(int2(pixel)) >= 1.0f)
        return false;

    uint drawWord = AGFXTexture2D<uint2>::Create(g_Constants.rVisibility).Load(int2(pixel)).x;
    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(SceneDrawWordInstance(drawWord));
    outMaterialSlot = instance.uMaterialSlot;
    return true;
}

// Binds the shared gbuffer decode to this path's constants. The scheme path binds the same function
// to its own -- see DeferredLoadSurface in DeferredShading.hlsli.
DeferredSurface ReSTIRLoadSurface(uint2 pixel, uint materialSlot) {
    GBufferHandles h;
    h.rFrameConstants = g_Constants.rFrameConstants;
    h.rDepth = g_Constants.rDepth;
    h.rAlbedo = g_Constants.rAlbedo;
    h.rNormal = g_Constants.rNormal;
    h.rMetallicRoughness = g_Constants.rMetallicRoughness;
    h.rEmissive = g_Constants.rEmissive;
    return GBufferLoadSurface(h, pixel, materialSlot, g_Constants.uWidth, g_Constants.uHeight);
}

// Binds the shared light-list walk to this path's constants. Sharing it with the scheme path is not
// tidiness: initial sampling has to draw its candidates from exactly the set the reference path
// evaluates, or the estimator converges to an image the reference never produces.
LightListHandles ReSTIRLightListHandles() {
    LightListHandles h;
    h.rFrameConstants = g_Constants.rFrameConstants;
    h.rClusterLights = g_Constants.rClusterLights;
    h.rLightCull = g_Constants.rLightCull;
    h.fSliceScale = g_Constants.fClusterSliceScale;
    h.fSliceBias = g_Constants.fClusterSliceBias;
    h.uClusteringEnabled = g_Constants.uClusteringEnabled;
    h.uLightCount = g_Constants.uLightCount;
    return h;
}

DeferredLightList ReSTIRBeginLights(float3 vWorldPosition, uint2 pixel) {
    return LightListBegin(ReSTIRLightListHandles(), vWorldPosition, pixel,
                          g_Constants.uWidth, g_Constants.uHeight);
}

uint ReSTIRLightIndex(DeferredLightList list, uint i) {
    return LightListIndex(ReSTIRLightListHandles(), list, i);
}

GPULight ReSTIRLoadLight(uint index) {
    return AGFXStructuredBuffer<GPULight>::Create(g_Constants.rLightBuffer).Load(index);
}

// Where this pixel's surface was last frame, in pixels. The motion gbuffer holds a screen-space UV
// delta (see GBufferResolve.hlsl); a caller still has to bounds-check the result, since last frame's
// position is routinely off screen.
float2 ReSTIRReproject(uint2 pixel) {
    float2 motion = AGFXTexture2D<float4>::Create(g_Constants.rMotion).Load(int2(pixel)).xy;
    return (float2(pixel) + 0.5f) - motion * float2(g_Constants.uWidth, g_Constants.uHeight);
}

// PCG-style hash, seeded per pixel and per frame. uFrameNumber is monotonic rather than the
// frame-in-flight slot precisely so the sequence does not repeat every three frames, which would
// make temporal reuse correlate with itself instead of accumulating new samples.
uint ReSTIRSeed(uint2 pixel) {
    uint seed = pixel.x * 73856093u ^ pixel.y * 19349663u ^ g_Constants.uFrameNumber * 83492791u;
    seed ^= seed >> 16;
    seed *= 0x7FEB352Du;
    seed ^= seed >> 15;
    seed *= 0x846CA68Bu;
    seed ^= seed >> 16;
    return seed;
}

float ReSTIRRandom(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word = (word >> 22u) ^ word;
    return float(word) * (1.0f / 4294967296.0f);
}

void ReSTIRWrite(uint2 pixel, float3 color) {
    AGFXRWTexture2D<float4>::Create(g_Constants.rOutput).Store(int2(pixel), float4(color, 1.0f));
}

#endif
