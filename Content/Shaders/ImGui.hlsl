/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 21:14:22
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Common/AGFX.hlsli"

#pragma vertex ImGuiVS
#pragma pixel ImGuiPS

struct ImGuiVertex {
    float2 vPosition;
    float2 vUV;
    uint uColor;
};

struct ImGuiPushConstants {
    float2 vScale;
    float2 vTranslate;
    uint uVtxOffset;
    ResourceHandle rVertexBuffer;
    ResourceHandle rTexture;
    ResourceHandle rSampler;
};
AGFX_PUSH_CONSTANTS(ImGuiPushConstants, g_Constants);

struct VSOut {
    float4 vPosition : SV_POSITION;
    float2 vUV : TEXCOORD0;
    float4 vColor : COLOR0;
};

VSOut ImGuiVS(uint uVID : SV_VertexID) {
    AGFXStructuredBuffer<ImGuiVertex> bVertices = AGFXStructuredBuffer<ImGuiVertex>::Create(g_Constants.rVertexBuffer);
    ImGuiVertex vertex = bVertices.Load(uVID + g_Constants.uVtxOffset);

    VSOut output;
    output.vPosition = float4(vertex.vPosition * g_Constants.vScale + g_Constants.vTranslate, 0.0f, 1.0f);
    output.vUV = vertex.vUV;

    uint packed = vertex.uColor;
    output.vColor = float4(
        (packed & 0xFF) / 255.0f,
        ((packed >> 8) & 0xFF) / 255.0f,
        ((packed >> 16) & 0xFF) / 255.0f,
        ((packed >> 24) & 0xFF) / 255.0f
    );
    return output;
}

float4 ImGuiPS(VSOut input) : SV_Target {
    AGFXTexture2D<float4> tTexture = AGFXTexture2D<float4>::Create(g_Constants.rTexture);
    AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);
    return input.vColor * tTexture.Sample(sSampler, input.vUV);
}
