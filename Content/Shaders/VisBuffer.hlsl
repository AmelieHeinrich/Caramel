/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Visibility pass: R = the draw word (instance | LOD | fade -- LOD is needed at resolve time to
// index the per-instance LOD table), G = meshletIndex << 7 | triangleIndex (124 triangles < 128).
// Alpha test and LOD dither must discard here -- they are the only reasons this PS isn't empty.

#include "Common/SceneMesh.hlsli"

#pragma pixel VisBufferPS

uint2 VisBufferPS(VSOut input, PrimOut prim) : SV_Target {
    SceneLodDither(input);

    GPUMaterial material = SceneLoadMaterial(input.uMaterialSlot);
    if (GPUMaterialIsAlphaTested(material)) {
        AGFXSampler sSampler = AGFXSampler::Create(g_Constants.rSampler);
        AGFXTexture2D<float4> tBaseColor = AGFXTexture2D<float4>::Create(material.uTextures[kMaterialTextureBaseColor]);
        float alpha = tBaseColor.Sample(sSampler, input.vUV).a * material.vBaseColorFactor.a;
        if (alpha < material.fAlphaCutoff)
            discard;
    }

    uint r = SceneMakeDrawWord(input.uInstanceIndex, input.uLOD, input.uFade & 0xFu, (input.uFade & 0x10u) != 0u);
    uint g = (input.uMeshletID << 7) | prim.uPrimitiveID;
    return uint2(r, g);
}
