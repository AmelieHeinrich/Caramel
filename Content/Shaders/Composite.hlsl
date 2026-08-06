/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 15:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Resolves the HDR scene lighting buffer into scene color, which carries the swap chain's format.
// The only reason this pass exists is that the deferred scheme dispatches write their output from
// compute, and a typed UAV on BGRA8Unorm is an optional D3D12 feature -- so shading targets an
// RGBA16F buffer and this brings it back. When a real tonemapper lands (Notes/TODO.md) it replaces
// the gamma curve below.

#include "Common/AGFX.hlsli"

#pragma vertex CompositeVS
#pragma pixel CompositePS

// Mirrors CompositePushConstants in Sources/Caramel/Renderer/SceneRenderer.cpp.
struct CompositePushConstants {
    ResourceHandle rSceneLighting;
    // A scene.gbuffer_debug view is already display-referred -- ID hashes, normals, motion vectors.
    // Running it through the gamma curve would just wash it out.
    uint uPassthrough;
};
AGFX_PUSH_CONSTANTS(CompositePushConstants, g_Constants);

struct CompositeVSOut {
    float4 vPosition : SV_POSITION;
};

CompositeVSOut CompositeVS(uint uVertexID : SV_VertexID) {
    float2 uv = float2((uVertexID << 1) & 2, uVertexID & 2);
    CompositeVSOut o;
    o.vPosition = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float4 CompositePS(CompositeVSOut input) : SV_Target {
    float3 color = AGFXTexture2D<float4>::Create(g_Constants.rSceneLighting).Load(int2(input.vPosition.xy)).rgb;
    if (g_Constants.uPassthrough == 0)
        color = pow(color, 1.0f / 2.2f);
    return float4(color, 1.0f);
}
