/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:40:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Common/AGFX.hlsli"

#pragma mesh SponzaMS
#pragma pixel SponzaPS

// Mirrors CaramelAsset::Vertex (Sources/CaramelAsset/Format.hpp) field-for-field.
struct Vertex {
    float3 vPosition;
    float3 vNormal;
    float4 vTangent; // xyz = tangent, w = handedness (unused here -- no normal mapping)
    float2 vUV;
};

// Mirrors CaramelAsset::MeshletDesc / meshopt_Meshlet exactly.
struct MeshletDesc {
    uint uVertexOffset;
    uint uTriangleOffset; // byte offset into the meshlet-triangle buffer, already 4-byte aligned
    uint uVertexCount;
    uint uTriangleCount;
};

struct FrameConstants {
    float4x4 mViewProj;
};

struct InstanceData {
    float4x4 mModel;
};

struct SponzaPushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceBuffer;
    uint uInstanceIndex;
    ResourceHandle rVertexBuffer;
    ResourceHandle rMeshletBuffer;
    ResourceHandle rMeshletVertexBuffer;
    ResourceHandle rMeshletTriangleBuffer;
    ResourceHandle rBaseColorTexture;
    ResourceHandle rSampler;
};
AGFX_PUSH_CONSTANTS(SponzaPushConstants, g_Constants);

struct VSOut {
    float4 vPosition : SV_POSITION;
    float3 vWorldNormal : NORMAL0;
    float2 vUV : TEXCOORD0;
};

// meshoptimizer packs meshlet-local triangle indices as 3 consecutive bytes per triangle (no
// 4-byte alignment between triangles, only meshlet.uTriangleOffset itself is 4-byte aligned), so a
// triangle's 3 bytes can straddle a 4-byte boundary. Load the 8-byte window starting at the
// containing aligned word and shift/mask out the 3 bytes we actually want.
uint3 UnpackTriangle(AGFXByteAddressBuffer buf, uint byteOffset) {
    uint wordOffset = byteOffset & ~3u;
    uint shift = (byteOffset - wordOffset) * 8;
    uint2 words = buf.Load2(wordOffset);
    uint packed = (shift == 0) ? words.x : ((words.x >> shift) | (words.y << (32 - shift)));
    return uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
}

// kMeshletMaxVertices / kMeshletMaxTriangles from Sources/CaramelAsset/Format.hpp.
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void SponzaMS(
    uint3 uGroupID : SV_GroupID,
    uint3 uGroupThreadID : SV_GroupThreadID,
    out indices uint3 outTriangles[124],
    out vertices VSOut outVertices[64])
{
    AGFXStructuredBuffer<MeshletDesc> bMeshlets = AGFXStructuredBuffer<MeshletDesc>::Create(g_Constants.rMeshletBuffer);
    MeshletDesc meshlet = bMeshlets.Load(uGroupID.x);

    SetMeshOutputCounts(meshlet.uVertexCount, meshlet.uTriangleCount);

    AGFXStructuredBuffer<FrameConstants> bFrame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants);
    AGFXStructuredBuffer<InstanceData> bInstances = AGFXStructuredBuffer<InstanceData>::Create(g_Constants.rInstanceBuffer);
    float4x4 mModel = bInstances.Load(g_Constants.uInstanceIndex).mModel;
    float4x4 mViewProj = bFrame.Load(0).mViewProj;

    AGFXStructuredBuffer<uint> bMeshletVertices = AGFXStructuredBuffer<uint>::Create(g_Constants.rMeshletVertexBuffer);
    AGFXStructuredBuffer<Vertex> bVertices = AGFXStructuredBuffer<Vertex>::Create(g_Constants.rVertexBuffer);

    for (uint v = uGroupThreadID.x; v < meshlet.uVertexCount; v += 32) {
        uint vertexIndex = bMeshletVertices.Load(meshlet.uVertexOffset + v);
        Vertex vertex = bVertices.Load(vertexIndex);

        float4 worldPosition = mul(mModel, float4(vertex.vPosition, 1.0f));

        VSOut o;
        o.vPosition = mul(mViewProj, worldPosition);
        o.vWorldNormal = mul((float3x3)mModel, vertex.vNormal); // uniform-scale assumption, no inverse-transpose
        o.vUV = vertex.vUV;
        outVertices[v] = o;
    }

    AGFXByteAddressBuffer bTriangles = AGFXByteAddressBuffer::Create(g_Constants.rMeshletTriangleBuffer);
    for (uint t = uGroupThreadID.x; t < meshlet.uTriangleCount; t += 32) {
        outTriangles[t] = UnpackTriangle(bTriangles, meshlet.uTriangleOffset + t * 3);
    }
}

float4 SponzaPS(VSOut input) : SV_Target {
    AGFXTexture2D<float4> tBaseColor = AGFXTexture2D<float4>::Create(g_Constants.rBaseColorTexture);
    AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);
    float4 baseColor = tBaseColor.Sample(sSampler, input.vUV);

    float3 normal = normalize(input.vWorldNormal);
    float3 lightDir = normalize(float3(-0.4f, 1.0f, -0.3f));
    float ndotl = max(dot(normal, lightDir), 0.0f);

    float3 ambient = baseColor.rgb * 0.25f;
    float3 diffuse = baseColor.rgb * ndotl * 0.85f;
    return float4(ambient + diffuse, baseColor.a);
}
