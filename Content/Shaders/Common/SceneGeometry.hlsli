/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Pragma-free geometry declarations shared between the mesh-shading path (SceneMesh.hlsli) and
// fullscreen consumers like GBufferResolve.hlsl, which must not inherit the task/mesh pragmas.

#ifndef CARAMEL_SCENEGEOMETRY_HLSL
#define CARAMEL_SCENEGEOMETRY_HLSL

#include "AGFX.hlsli"

// Mirrors CaramelAsset::Vertex (Sources/CaramelAsset/Format.hpp) field-for-field.
struct Vertex {
    float3 vPosition;
    float3 vNormal;
    float4 vTangent; // xyz = tangent, w = handedness
    float2 vUV;
};

// Mirrors CaramelAsset::MeshletDesc / meshopt_Meshlet exactly.
struct MeshletDesc {
    uint uVertexOffset;
    uint uTriangleOffset; // byte offset into the meshlet-triangle buffer, already 4-byte aligned
    uint uVertexCount;
    uint uTriangleCount;
};

// Mirrors FrameConstants in Sources/Caramel/Renderer/SceneRenderer.cpp field-for-field.
struct FrameConstants {
    float4x4 mView;
    float4x4 mProjection;
    float4x4 mViewProjection;
    float4x4 mInvView;
    float4x4 mInvProjection;
    float4x4 mInvViewProjection;
    float4   vFrustumPlanes[6]; // Left/Right/Bottom/Top/Near/Far, xyz = normal, w = distance
    float3   vCameraPosition;
    float    fNearPlane;
    float    fFarPlane;
    float3   _Pad;
    float4x4 mHZBViewProjection; // the camera the current pyramid was rasterized from
    float4x4 mPrevViewProjection; // last frame's live viewProjection, for motion vectors
};

uint3 UnpackTriangle(AGFXByteAddressBuffer buf, uint byteOffset) {
    uint wordOffset = byteOffset & ~3u;
    uint shift = (byteOffset - wordOffset) * 8;
    uint2 words = buf.Load2(wordOffset);
    uint packed = (shift == 0) ? words.x : ((words.x >> shift) | (words.y << (32 - shift)));
    return uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
}

#endif
