/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Populates the Opaque indirect bundle: one DrawMesh command per resident scene instance, no
// culling. Bootstraps the indirect-submission path so a real culling shader can slot in later
// without touching the prepare/execute plumbing around it.

#include "Common/AGFX.hlsli"
#include "Common/GPUScene.hlsli"

#pragma compute PopulateOpaqueIndirectBundleCS

struct PopulatePushConstants {
    ResourceHandle rInstanceBuffer;
    uint           uInstanceCount;
    uint64_t       uBundleHandle;
    ResourceHandle rDrawIndirection; // valid on every backend; only read/written on Vulkan
};
AGFX_PUSH_CONSTANTS(PopulatePushConstants, g_Constants);

[numthreads(64, 1, 1)]
void PopulateOpaqueIndirectBundleCS(uint3 dtid : SV_DispatchThreadID) {
    uint index = dtid.x;
    if (index >= g_Constants.uInstanceCount)
        return;

    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(index);

    AGFXIndirectDrawMeshBundle bundle = AGFXIndirectDrawMeshBundle::Create(g_Constants.uBundleHandle);
    uint slot = bundle.DrawMesh(/*commandOffset*/ 0, /*countIndex*/ 0, /*drawId*/ index, instance.uMeshletCount, 1, 1);

#if defined(AGFX_VULKAN)
    // Vulkan's AGFX_DRAW_ID() in the consuming mesh shader is the linear slot, not the drawId
    // written above -- record slot -> index so the consumer can recover it (SKILL.md gotcha 6).
    AGFXRWByteAddressBuffer indirection = AGFXRWByteAddressBuffer::Create(g_Constants.rDrawIndirection);
    indirection.Store(slot * 4, index);
#endif
}
