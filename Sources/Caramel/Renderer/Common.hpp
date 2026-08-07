/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:19:23
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <AGFX/agfx.hpp>

#define FRAMES_IN_FLIGHT 3

// HDR shading buffer sitting between the gbuffer and scene color. Shared here because three places
// have to agree on it: the texture Renderer creates, the GBuffer Resolve pipeline's attachment 0,
// and the Composite pipeline that reads it. RGBA16F rather than the swap chain format because the
// scheme dispatches write it from compute, and a typed UAV on BGRA8Unorm is optional in D3D12.
constexpr agfx::TextureFormat kSceneLightingFormat = agfx::TextureFormat::RGBA16F;

// The single-pass downsampler writes mips 0..12 in one dispatch, which covers an HZB up to 4096 on
// its longest side. Nothing deeper is reachable without a second dispatch.
constexpr uint32 kMaxHZBMips = 13;

// Order is the attachment contract between the GBuffer Resolve pipeline and GBufferOut in
// GBufferResolve.hlsl: albedo, normal, metallic/roughness, emissive, motion.
constexpr uint32 kGBufferTextureCount = 5;

// Everything SceneRenderer needs to build and sample the depth pyramid. Owned by Renderer (the HZB
// outlives any single frame's render graph, and a mip chain needs one writeable view per mip, which
// RGResolveContext::ResolveBindlessTexture cannot produce), handed down per pass.
// Bindless handles the deferred material passes read and write. Same ownership split as
// HZBResources: Renderer owns the textures (they outlive any one frame's render graph, and the
// lighting buffer needs a writeable view RGResolveContext::ResolveBindlessTexture cannot produce),
// SceneRenderer just consumes the handles. Gbuffer order matches Renderer::m_GBufferTextures and
// GBufferOut in GBufferResolve.hlsl.
struct DeferredTargets
{
    uint32 visibilityHandle;
    uint32 depthHandle;
    uint32 albedoHandle;
    uint32 normalHandle;
    uint32 metallicRoughnessHandle;
    uint32 emissiveHandle;
    uint32 sceneLightingUAVHandle;   // writeable view -- what the scheme dispatches store into
};

// What the clustered light grid looks like to everything downstream of the culling passes. Owned by
// ClusteredLightPass -- same ownership split as HZBResources, for the same reason: the grid buffers
// outlive any one frame's render graph, and the graph cannot hand out a bindless *buffer* handle at
// all (RGResolveContext resolves textures only). Grid dimensions are not in here: they are compile-
// time constants in Content/Shaders/Common/ClusteredLights.hlsli, mirrored by kCluster* below.
struct ClusterResources
{
    uint32 clusterLightsHandle;  // raw view: per-cluster counts, then per-cluster index lists
    uint32 lightCullHandle;      // raw view: counters + the directional light index list
    float32 sliceScale;          // view depth -> cluster slice, as slice = log2(z) * scale + bias
    float32 sliceBias;
    uint32 enabled;              // 0 while scene.clustered_lights is off -- shading falls back to
                                 // walking every light, which is the ground truth to diff against
};

// Mirror Content/Shaders/Common/ClusteredLights.hlsli. The CPU side only needs these to size the
// buffers and to report overflow; everything else about the grid lives in the shader header.
constexpr uint32 kClusterGridX = 16;
constexpr uint32 kClusterGridY = 9;
constexpr uint32 kClusterGridZ = 24;
constexpr uint32 kClusterCount = kClusterGridX * kClusterGridY * kClusterGridZ;
constexpr uint32 kMaxLightsPerCluster = 128;
constexpr uint32 kMaxDirectionalLights = 8;

struct HZBResources
{
    uint32 depthHandle;              // read-only SRV over the depth buffer, source of the pyramid
    uint32 depthWidth;
    uint32 depthHeight;
    uint32 hzbHandle;                // read-only view over the whole chain, what culling samples
    uint32 mipHandles[kMaxHZBMips];  // writeable view per mip, what the downsampler writes
    uint32 width;                    // mip 0 dimensions -- the depth buffer's, rounded down to a power of two
    uint32 height;
    uint32 mipCount;
};
