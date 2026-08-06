/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Fullscreen visibility-buffer resolve: re-fetches each pixel's triangle from the meshlet buffers,
// reconstructs barycentrics + derivatives analytically (CalcFullBary), and writes the whole gbuffer
// with SampleGrad mip selection. Deliberately does NOT include SceneMesh.hlsli -- ShaderParser
// inlines includes before scanning pragmas, so its task/mesh pragmas would leak into this pipeline.

#include "Common/AGFX.hlsli"
#include "Common/GPUScene.hlsli"
#include "Common/SceneGeometry.hlsli"
#include "Common/VisibilityBuffer.hlsli"

#pragma vertex GBufferResolveVS
#pragma pixel GBufferResolvePS

// Mirrors GBufferResolvePushConstants in Sources/Caramel/Renderer/SceneRenderer.cpp.
struct GBufferResolvePushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceBuffer;
    ResourceHandle rMaterialBuffer;
    ResourceHandle rInstanceLodTable;
    ResourceHandle rVisibility;
    ResourceHandle rDepth;
    ResourceHandle rSampler;
    ResourceHandle rFallbackTexture;
    uint uDebugMode; // scene.gbuffer_debug (see Core/CVar.hpp)
    uint uWidth;
    uint uHeight;
};
AGFX_PUSH_CONSTANTS(GBufferResolvePushConstants, g_Constants);

static const uint kGBufferDebugAlbedo = 0;
static const uint kGBufferDebugNormal = 1;
static const uint kGBufferDebugMetallicRoughness = 2;
static const uint kGBufferDebugEmissive = 3;
static const uint kGBufferDebugMotion = 4;
static const uint kGBufferDebugMeshletId = 5;
static const uint kGBufferDebugTriangleId = 6;
static const uint kGBufferDebugInstanceId = 7;
static const uint kGBufferDebugLod = 8;

struct ResolveVSOut {
    float4 vPosition : SV_POSITION;
};

ResolveVSOut GBufferResolveVS(uint uVertexID : SV_VertexID) {
    float2 uv = float2((uVertexID << 1) & 2, uVertexID & 2);
    ResolveVSOut o;
    o.vPosition = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

struct GBufferOut {
    float4 vSceneColor        : SV_Target0;
    float4 vAlbedo            : SV_Target1;
    float4 vNormal            : SV_Target2;
    float2 vMetallicRoughness : SV_Target3;
    float4 vEmissive          : SV_Target4;
    float2 vMotion            : SV_Target5;
};

uint WangHash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16);
    seed *= 9u;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15);
    return seed;
}

float3 IdToColor(uint id) {
    uint h = WangHash(id);
    return float3(
        (float)(h & 0xFFu) / 255.0f,
        (float)((h >> 8) & 0xFFu) / 255.0f,
        (float)((h >> 16) & 0xFFu) / 255.0f);
}

float3 DebugView(uint mode, GBufferOut gbuffer, uint instanceIndex, uint meshletIndex, uint triangleIndex, uint lod) {
    switch (mode) {
        case kGBufferDebugNormal: return gbuffer.vNormal.xyz * 0.5f + 0.5f;
        case kGBufferDebugMetallicRoughness: return float3(gbuffer.vMetallicRoughness, 0.0f);
        case kGBufferDebugEmissive: return gbuffer.vEmissive.rgb;
        case kGBufferDebugMotion: return float3(abs(gbuffer.vMotion) * 10.0f, 0.0f);
        case kGBufferDebugMeshletId: return IdToColor(instanceIndex * 9781u + meshletIndex);
        case kGBufferDebugTriangleId: return IdToColor((instanceIndex * 9781u + meshletIndex) * 4139u + triangleIndex);
        case kGBufferDebugInstanceId: return IdToColor(instanceIndex);
        case kGBufferDebugLod: {
            // Red = coarsest .. blue = finest; a cross-fade shows as two colors dissolving into each other.
            const float3 lodColors[kLodCount] = {
                float3(1.0f, 0.2f, 0.2f),
                float3(1.0f, 0.6f, 0.1f),
                float3(1.0f, 1.0f, 0.2f),
                float3(0.3f, 1.0f, 0.3f),
                float3(0.3f, 0.6f, 1.0f),
            };
            return lodColors[min(lod, kLodCount - 1)];
        }
        default: return gbuffer.vAlbedo.rgb;
    }
}

GBufferOut GBufferResolvePS(ResolveVSOut input) {
    GBufferOut o = (GBufferOut)0;

    int2 pixel = int2(input.vPosition.xy);

    AGFXTexture2D<float> tDepth = AGFXTexture2D<float>::Create(g_Constants.rDepth);
    if (tDepth.Load(pixel) >= 1.0f)
        return o;

    uint2 vis = AGFXTexture2D<uint2>::Create(g_Constants.rVisibility).Load(pixel);
    uint instanceIndex = SceneDrawWordInstance(vis.x);
    uint lod = SceneDrawWordLod(vis.x);
    uint meshletIndex = VisMeshlet(vis.y);
    uint triangleIndex = VisTriangle(vis.y);

    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants).Load(0);
    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(instanceIndex);
    GPUMaterial material = AGFXStructuredBuffer<GPUMaterial>::Create(g_Constants.rMaterialBuffer).Load(instance.uMaterialSlot);
    GPULodInfo lodInfo = SceneLoadLodInfo(g_Constants.rInstanceLodTable, instanceIndex, lod);

    MeshletDesc meshlet = AGFXStructuredBuffer<MeshletDesc>::Create(lodInfo.rMeshletBuffer).Load(meshletIndex);
    uint3 localIndices = UnpackTriangle(AGFXByteAddressBuffer::Create(lodInfo.rMeshletTriangleBuffer), meshlet.uTriangleOffset + triangleIndex * 3);

    AGFXStructuredBuffer<uint> bMeshletVertices = AGFXStructuredBuffer<uint>::Create(lodInfo.rMeshletVertexBuffer);
    AGFXStructuredBuffer<Vertex> bVertices = AGFXStructuredBuffer<Vertex>::Create(instance.rVertexBuffer);
    Vertex v0 = bVertices.Load(bMeshletVertices.Load(meshlet.uVertexOffset + localIndices.x));
    Vertex v1 = bVertices.Load(bMeshletVertices.Load(meshlet.uVertexOffset + localIndices.y));
    Vertex v2 = bVertices.Load(bMeshletVertices.Load(meshlet.uVertexOffset + localIndices.z));

    float4x4 mModel = instance.mTransform;
    float4 world0 = mul(mModel, float4(v0.vPosition, 1.0f));
    float4 world1 = mul(mModel, float4(v1.vPosition, 1.0f));
    float4 world2 = mul(mModel, float4(v2.vPosition, 1.0f));
    float4 clip0 = mul(frame.mViewProjection, world0);
    float4 clip1 = mul(frame.mViewProjection, world1);
    float4 clip2 = mul(frame.mViewProjection, world2);

    float2 winSize = float2(g_Constants.uWidth, g_Constants.uHeight);
    float2 pixelCenter = input.vPosition.xy;
    float2 pixelNdc = float2(pixelCenter.x / winSize.x * 2.0f - 1.0f, 1.0f - pixelCenter.y / winSize.y * 2.0f);
    BarycentricDeriv bary = CalcFullBary(clip0, clip1, clip2, pixelNdc, winSize);

    float2 uv, uvDdx, uvDdy;
    BaryInterpolateUV(bary, v0.vUV, v1.vUV, v2.vUV, uv, uvDdx, uvDdy);

    float3x3 mNormal = (float3x3)mModel;
    float3 geometricNormal = normalize(BaryInterpolate3(bary, mul(mNormal, v0.vNormal), mul(mNormal, v1.vNormal), mul(mNormal, v2.vNormal)));
    float4 tangent0 = float4(mul(mNormal, v0.vTangent.xyz), v0.vTangent.w);
    float4 tangent1 = float4(mul(mNormal, v1.vTangent.xyz), v1.vTangent.w);
    float4 tangent2 = float4(mul(mNormal, v2.vTangent.xyz), v2.vTangent.w);
    float4 worldTangent = BaryInterpolate4(bary, tangent0, tangent1, tangent2);

    AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);

    AGFXTexture2D<float4> tBaseColor = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureBaseColor]);
    float4 baseColor = tBaseColor.SampleGrad(sSampler, uv, uvDdx, uvDdy) * material.vBaseColorFactor;

    // An overridden channel takes the factor as-is -- multiplying it into the texture would still
    // leave a 0 texel at 0 no matter the factor, defeating the point of forcing a value.
    float roughness = material.fRoughnessFactor;
    float metallic = material.fMetallicFactor;
    if (material.uTextures[kMaterialTextureMetallicRoughness] != g_Constants.rFallbackTexture) {
        AGFXTexture2D<float4> tMetallicRoughness = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureMetallicRoughness]);
        float4 metallicRoughness = tMetallicRoughness.SampleGrad(sSampler, uv, uvDdx, uvDdy);
        if (!GPUMaterialOverridesRoughness(material))
            roughness = saturate(metallicRoughness.g) * material.fRoughnessFactor;
        if (!GPUMaterialOverridesMetallic(material))
            metallic = saturate(metallicRoughness.b) * material.fMetallicFactor;
    }
    roughness = saturate(roughness);
    metallic = saturate(metallic);

    float3 normal = geometricNormal;
    if (material.uTextures[kMaterialTextureNormal] != g_Constants.rFallbackTexture) {
        float3 tangent = normalize(worldTangent.xyz);
        tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
        float3 bitangent = cross(geometricNormal, tangent) * worldTangent.w;

        AGFXTexture2D<float4> tNormal = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureNormal]);
        float2 tangentNormalXY = tNormal.SampleGrad(sSampler, uv, uvDdx, uvDdy).rg * 2.0f - 1.0f;
        float tangentNormalZ = sqrt(saturate(1.0f - dot(tangentNormalXY, tangentNormalXY)));
        float3 tangentNormal = float3(tangentNormalXY, tangentNormalZ);
        float3x3 mTBN = float3x3(tangent, bitangent, geometricNormal);
        normal = normalize(mul(tangentNormal, mTBN));
    }

    float3 emissive = material.vEmissiveFactor.rgb;
    if (material.uTextures[kMaterialTextureEmissive] != g_Constants.rFallbackTexture) {
        AGFXTexture2D<float4> tEmissive = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureEmissive]);
        emissive *= tEmissive.SampleGrad(sSampler, uv, uvDdx, uvDdy).rgb;
    }

    float4 prevClip0 = mul(frame.mPrevViewProjection, mul(instance.mPrevTransform, float4(v0.vPosition, 1.0f)));
    float4 prevClip1 = mul(frame.mPrevViewProjection, mul(instance.mPrevTransform, float4(v1.vPosition, 1.0f)));
    float4 prevClip2 = mul(frame.mPrevViewProjection, mul(instance.mPrevTransform, float4(v2.vPosition, 1.0f)));
    float4 prevClip = BaryInterpolate4(bary, prevClip0, prevClip1, prevClip2);
    float2 prevUV = (prevClip.xy / prevClip.w) * float2(0.5f, -0.5f) + 0.5f;
    float2 currUV = pixelCenter / winSize;

    o.vAlbedo = float4(baseColor.rgb, 1.0f);
    o.vNormal = float4(normal, 0.0f);
    o.vMetallicRoughness = float2(metallic, roughness);
    o.vEmissive = float4(emissive, 0.0f);
    o.vMotion = currUV - prevUV;
    o.vSceneColor = float4(DebugView(g_Constants.uDebugMode, o, instanceIndex, meshletIndex, triangleIndex, lod), 1.0f);
    return o;
}
