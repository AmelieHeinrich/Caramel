/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 14:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Shared body of the immediate-mode debug renderer's vertex/pixel shaders. Included by both
// DebugDrawDepth.hlsl and DebugDrawOverlay.hlsl, which differ only in the pipeline template the
// C++ side registers for them (depth-tested vs. always-on-top) -- ShaderServer keys pipeline
// templates by shader path, so two depth states need two paths.
//
// Three vertex-shader variants, selected by the DEBUG_LINE / DEBUG_POINT defines that ShaderServer
// derives from `#pragma variant`:
//   (none)      -- solid geometry, one DebugVertex per SV_VertexID.
//   DEBUG_LINE  -- one DebugSegment expanded into a screen-space quad (6 verts per segment).
//   DEBUG_POINT -- one DebugPointPrim expanded into a screen-space square (6 verts per point).
// Lines/points are expanded here rather than rasterized with Topology::Lines/Points because AGFX
// exposes no line width, and expansion gives exact pixel thickness at any distance.

#ifndef CARAMEL_DEBUG_DRAW_HLSL
#define CARAMEL_DEBUG_DRAW_HLSL

#include "AGFX.hlsli"

// All three mirror the matching C++ structs in DebugRenderer.cpp field-for-field (32 bytes each).
struct DebugSegment {
    float3 vA;
    uint uColor;
    float3 vB;
    float fThickness; // Pixels.
};

struct DebugPointPrim {
    float3 vP;
    uint uColor;
    float fSize;      // Pixels.
    float3 vPad;
};

struct DebugVertex {
    float3 vP;
    uint uColor;
    float3 vN;        // World-space face normal, for the headlight term.
    float fPad;
};

struct DebugFrameConstants {
    float4x4 mViewProj;
    float3 vCameraPos;
    float fPad;
    float2 vViewport;
    float2 vInvViewport;
};

struct DebugPushConstants {
    ResourceHandle rFrameConstants;
    ResourceHandle rPrims;
    uint uBaseIndex;  // Element offset into rPrims; the depth-tested and overlay buckets share one
                      // buffer, so the overlay draw starts partway in (same trick as ImGui's uVtxOffset).
    uint uPad;
};
AGFX_PUSH_CONSTANTS(DebugPushConstants, g_Constants);

struct VSOut {
    float4 vPosition : SV_POSITION;
    float4 vColor : COLOR0;
    float3 vToCamera : TEXCOORD0;
    float3 vNormal : NORMAL0;
};

// Anything at or behind this clip-space w is treated as behind the camera. Not zero: the
// perspective divide below has to stay finite.
static const float kMinClipW = 1e-4f;

float4 UnpackColor(uint packed)
{
    return float4(
        (packed & 0xFF) / 255.0f,
        ((packed >> 8) & 0xFF) / 255.0f,
        ((packed >> 16) & 0xFF) / 255.0f,
        ((packed >> 24) & 0xFF) / 255.0f
    );
}

#if defined(DEBUG_LINE) || defined(DEBUG_POINT)
// Two triangles over corners 0..3, wound so either face is fine (the pipelines cull nothing).
static const uint kQuadIndices[6] = { 0, 1, 2, 2, 1, 3 };
#endif

VSOut DebugVS(uint uVID : SV_VertexID)
{
    DebugFrameConstants frame = AGFXStructuredBuffer<DebugFrameConstants>::Create(g_Constants.rFrameConstants).Load(0);

    VSOut output;
    output.vPosition = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.vColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    output.vToCamera = float3(0.0f, 0.0f, 0.0f);
    output.vNormal = float3(0.0f, 0.0f, 0.0f);

#if defined(DEBUG_LINE) || defined(DEBUG_POINT)
    uint uPrim = (uVID / 6) + g_Constants.uBaseIndex;
    uint uQuad = kQuadIndices[uVID % 6];
#endif

#if defined(DEBUG_LINE)

    DebugSegment segment = AGFXStructuredBuffer<DebugSegment>::Create(g_Constants.rPrims).Load(uPrim);

    float4 vClipA = mul(frame.mViewProj, float4(segment.vA, 1.0f));
    float4 vClipB = mul(frame.mViewProj, float4(segment.vB, 1.0f));

    // Debug lines routinely straddle the camera plane, and a naive divide by a negative w mirrors
    // the endpoint to the wrong side of the screen. Clip the segment against w = kMinClipW first:
    // fully behind collapses to a degenerate triangle, partly behind gets its endpoint pulled onto
    // the plane (a lerp in clip space, which is where the segment is still straight).
    if (vClipA.w <= kMinClipW && vClipB.w <= kMinClipW)
        return output;
    if (vClipA.w <= kMinClipW)
        vClipA = lerp(vClipA, vClipB, (kMinClipW - vClipA.w) / (vClipB.w - vClipA.w));
    else if (vClipB.w <= kMinClipW)
        vClipB = lerp(vClipB, vClipA, (kMinClipW - vClipB.w) / (vClipA.w - vClipB.w));

    float2 vPixelA = (vClipA.xy / vClipA.w) * 0.5f * frame.vViewport;
    float2 vPixelB = (vClipB.xy / vClipB.w) * 0.5f * frame.vViewport;

    float2 vDelta = vPixelB - vPixelA;
    float2 vDir = (dot(vDelta, vDelta) < 1e-8f) ? float2(1.0f, 0.0f) : normalize(vDelta);
    float2 vOffset = float2(-vDir.y, vDir.x) * (max(segment.fThickness, 1.0f) * 0.5f);

    bool bIsEndB = (uQuad >= 2);
    bool bIsNegativeSide = ((uQuad & 1) != 0);
    float4 vClip = bIsEndB ? vClipB : vClipA;
    float2 vPixel = (bIsEndB ? vPixelB : vPixelA) + (bIsNegativeSide ? -vOffset : vOffset);

    // Each corner keeps its own endpoint's z/w, so depth stays correct along the segment; the
    // multiply by w undoes the divide the rasterizer is about to redo.
    output.vPosition = float4(vPixel * 2.0f * frame.vInvViewport * vClip.w, vClip.z, vClip.w);
    output.vColor = UnpackColor(segment.uColor);
    output.vToCamera = bIsEndB ? segment.vB : segment.vA;

#elif defined(DEBUG_POINT)

    DebugPointPrim point_ = AGFXStructuredBuffer<DebugPointPrim>::Create(g_Constants.rPrims).Load(uPrim);

    float4 vClip = mul(frame.mViewProj, float4(point_.vP, 1.0f));
    if (vClip.w <= kMinClipW)
        return output;

    float2 vPixel = (vClip.xy / vClip.w) * 0.5f * frame.vViewport;
    float2 vCorner = float2(((uQuad & 1) != 0) ? 1.0f : -1.0f, (uQuad >= 2) ? 1.0f : -1.0f);
    vPixel += vCorner * (max(point_.fSize, 1.0f) * 0.5f);

    output.vPosition = float4(vPixel * 2.0f * frame.vInvViewport * vClip.w, vClip.z, vClip.w);
    output.vColor = UnpackColor(point_.uColor);
    output.vToCamera = point_.vP;

#else

    DebugVertex vertex = AGFXStructuredBuffer<DebugVertex>::Create(g_Constants.rPrims).Load(uVID + g_Constants.uBaseIndex);

    output.vPosition = mul(frame.mViewProj, float4(vertex.vP, 1.0f));
    output.vColor = UnpackColor(vertex.uColor);
    output.vToCamera = vertex.vP;
    output.vNormal = vertex.vN;

#endif

    // Interpolate the vector towards the eye rather than the world position -- the pixel shader
    // only ever needs the view direction, and this keeps it from having to re-read the constants.
    output.vToCamera = frame.vCameraPos - output.vToCamera;
    return output;
}

float4 DebugPS(VSOut input) : SV_Target
{
    float4 vColor = input.vColor;

#if !defined(DEBUG_LINE) && !defined(DEBUG_POINT)
    // Headlight wrap-lambert so a filled sphere reads as a sphere and not a disc. abs() because
    // nothing is culled, so back faces arrive with an inverted normal.
    float fNdotV = abs(dot(normalize(input.vNormal), normalize(input.vToCamera)));
    vColor.rgb *= lerp(0.45f, 1.0f, fNdotV);
#endif

    return vColor;
}

#endif // CARAMEL_DEBUG_DRAW_HLSL
