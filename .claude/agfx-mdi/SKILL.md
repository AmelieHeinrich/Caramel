---
name: agfx-mdi
description: ALWAYS use when building GPU-driven draw submission in AGFX — creating an agfxIndirectBundle, appending draws to it from a culling/compute shader via the AGFXIndirectDraw*Bundle HLSL helpers, or preparing/replaying one with agfxComputePassPrepareIndirectBundle / agfxRenderPassExecuteIndirectBundle / agfxComputePassExecuteIndirectBundle. Trigger for agfxIndirectBundleCreate/Destroy/GetHandle/GetCommandsBuffer/GetCountBuffer, agfxIndirectBundleExecuteInfo, agfxIndirectBundleType, AGFX_INDIRECT_BUNDLE_TYPE_DRAW/DRAW_INDEXED/DRAW_MESH/DISPATCH, agfxDrawCommand/agfxDrawIndexedCommand/agfxDrawMeshCommand/agfxDispatchCommand, AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT, supportsMultiDrawIndirect, supportsIndirect, AGFXIndirectDrawBundle/AGFXIndirectDrawIndexedBundle/AGFXIndirectDrawMeshBundle/AGFXIndirectDispatchBundle, agfx::IndirectBundle, agfx::ez::IndirectBundle, Context::CreateIndirectBundle/ExecuteIndirectBundle/TransitionIndirectBundle, "multi draw indirect", "MDI", "ExecuteIndirect", "GPU-driven rendering", "GPU culling", "indirect command buffer", "ICB". Do NOT trigger for general barrier/fence semantics unrelated to indirect bundles — use agfx-synchronization. Do NOT trigger for the bindless shader-authoring mechanics themselves (ResourceHandle, AGFX_PUSH_CONSTANTS) — use agfx-writing-bindless-shaders.
---

# AGFX Multi-Draw Indirect (GPU-driven submission)

## Overview

An **indirect bundle** lets a compute shader decide, on the GPU, how many draws happen and what their arguments are — frustum culling, LOD selection, batch compaction — with the host issuing a single replay call. A bundle owns two GPU buffers: a **commands buffer** (one `agfxDraw*Command` struct per draw) and a **count buffer** (one or more `uint32_t` slots the producing shader atomically increments).

The buffer layout is **D3D12-command-signature-shaped** on every backend, so the culling shader writes one format regardless of platform. The backends then diverge entirely on the host side:

- **D3D12**: `ExecuteIndirect` consumes the commands + count buffers directly at submit time. `PrepareIndirectBundle` is a real but empty function.
- **Vulkan**: `vkCmdDraw*IndirectCount` likewise consumes both buffers directly at execute time; `PrepareIndirectBundle` is empty, same as D3D12. The backend skips each command's leading `drawID` field by offsetting the first read 4 bytes in and striding over it — but the drawID itself never reaches the shader; see gotcha 6.
- **Metal**: there is no "replay this buffer of draws" primitive. Draws must be pre-encoded into an `MTLIndirectCommandBuffer`, one command per draw, by a compute kernel. `PrepareIndirectBundle` does that translation; `ExecuteIndirectBundle` is a thin `executeCommandsInBuffer:` shim.

That asymmetry is the source of nearly every bug in this area: **code can be completely correct on D3D12/Vulkan and silently wrong on Metal** (prepare-time requirements, gotchas 1–2), and **correct on D3D12/Metal while reading the wrong per-draw data on Vulkan** (the drawID divergence, gotcha 6). The design rationale lives in `notes/mdi.md`. The reference consumer is the demo's GPU-driven G-buffer path: `data/shaders/demo/culling.hlsl`, `data/shaders/demo/gbuffer_indirect.hlsl`, `src/agfx/agfx_demo/culling.cpp`, and `deferred_renderer.cpp`'s `CullGBuffer` / `RenderGBuffer`.

## Ownership

**Owns:**
- Bundle lifecycle: `agfxIndirectBundleCreate` / `Destroy` / `GetHandle` / `GetCommandsBuffer` / `GetCountBuffer`
- `agfxIndirectBundleCreateInfo` (type, `maxCommandCount`, `maxCountCount`) and `agfxIndirectBundleExecuteInfo` (`countIndex`, `commandOffset`, per-call `maxCommandCount`, `pushConstants`, pipelines, `indexBuffer`)
- The command structs and their exact memory layout: `agfxDrawCommand` (20 B), `agfxDrawIndexedCommand` (24 B), `agfxDrawMeshCommand` (16 B), `agfxDispatchCommand` (12 B)
- The prepare/execute call sequence and its barrier requirements
- Shader-side appending: `AGFXIndirectDrawBundle`, `AGFXIndirectDrawIndexedBundle`, `AGFXIndirectDrawMeshBundle`, `AGFXIndirectDispatchBundle`
- Capability gating via `agfxDeviceGetInfo().supportsMultiDrawIndirect`, and the `supportsIndirect` pipeline flag
- The Metal ICB-conversion implementation (for anyone editing `agfx_metal4.mm`)
- C++/ez wrappers: `agfx::IndirectBundle`, `agfx::ez::IndirectBundle`, `Context::CreateIndirectBundle` / `ExecuteIndirectBundle` / `TransitionIndirectBundle`

**Does NOT own** (delegate):
- General barrier stage semantics, the `agglomerate` flag, fences, frames-in-flight → **agfx-synchronization** (this skill names only the indirect-specific transitions)
- `AGFX_PUSH_CONSTANTS`, `ResourceHandle`, `AGFXRWByteAddressBuffer` mechanics → **agfx-writing-bindless-shaders**
- Render pass / attachment authoring around the replay → **agfx-render-targets-and-passes**

## Capability gate

```cpp
agfxDeviceInfo info = {};
agfxDeviceGetInfo(device, &info);
if (!info.supportsMultiDrawIndirect) { /* fall back to the CPU draw loop */ }
```

Always keep the non-indirect path alive as a fallback. The demo does this with a `gpuDrivenSettings.enabled` toggle plus a readiness check (`indirectPrimitiveCount == scene.primitives.size()`), which is also what makes A/B comparison possible when the indirect output looks wrong.

## The call sequence

Everything is rebuilt every frame; bundle contents are scoped to the frame that builds them.

```cpp
// 0. WAR: the previous frame may still be replaying this bundle (see gotcha 5).
agfxCommandBufferMemoryBarrier(cmd,
    AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, true);

agfxComputePass* pass = agfxComputePassBegin(cmd, "Culling");

// 1. Reset the count slot, then transition it for atomic appends.
agfxComputePassCopyBufferToBuffer(pass, zeroBuffer, countBuffer, 0, 0, sizeof(uint32_t));
agfxCommandBufferMemoryBarrier(cmd,
    AGFX_RESOURCE_STATE_COPY_DEST, AGFX_RESOURCE_STATE_UNORDERED_ACCESS, true);

// 2. The producing shader appends draws + increments the count.
culling->Cull(pass, ...., agfxIndirectBundleGetHandle(bundle), drawIndirectionHandle);

// 3. UAV barriers: the prepare kernel reads what the culling dispatch just wrote.
agfxComputePassBufferUAVBarrier(pass, commandsBuffer);
agfxComputePassBufferUAVBarrier(pass, countBuffer);

// 4. Prepare. No-op on D3D12 and Vulkan; builds the ICB on Metal.
agfxIndirectBundleExecuteInfo prepareInfo = {};
prepareInfo.countIndex = 0;
prepareInfo.commandOffset = 0;
prepareInfo.maxCommandCount = primitiveCount;
memcpy(prepareInfo.pushConstants, &pc, sizeof(pc));   // REQUIRED — see gotcha 2
prepareInfo.renderPipeline = indirectPipeline;
prepareInfo.indexBuffer = scene.indexBuffer;
agfxComputePassPrepareIndirectBundle(pass, bundle, &prepareInfo);

agfxComputePassEnd(pass);

// 5. Hand off to the replay. agfxCommandBufferMemoryBarrier isn't scoped to a resource (see
// agfx-synchronization), so one call orders both the commands and count buffers together.
agfxCommandBufferMemoryBarrier(cmd,
    AGFX_RESOURCE_STATE_UNORDERED_ACCESS, AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT, true);

// 6. Replay, inside a render pass (or a compute pass for DISPATCH bundles).
agfxRenderPassExecuteIndirectBundle(renderPass, bundle, &executeInfo);
```

`PrepareIndirectBundle` must run in a **compute** pass; `ExecuteIndirectBundle` exists on both render passes (DRAW/DRAW_INDEXED/DRAW_MESH) and compute passes (DISPATCH).

## Critical gotchas (all learned the hard way)

### 1. The pipeline must set `supportsIndirect = true`
`agfxRenderPipelineCreateInfo::supportsIndirect` maps to Metal's `supportIndirectCommandBuffers`. Without it the PSO cannot legally be referenced from an ICB, and the result is a **GPU address fault / black screen**, not a validation message:
```
IOGPUMetalError: Caused GPU Address Fault Error (kIOGPUCommandBufferCallbackErrorPageFault)
```
Neither D3D12 nor Vulkan has an equivalent requirement, so forgetting this is invisible everywhere except macOS. `agfxComputePipelineCreateInfo` has no such flag — the Metal backend enables it unconditionally for compute PSOs.

### 2. `pushConstants` must be set on the **prepare** info, not just the execute info
Prepare and Execute deliberately share `agfxIndirectBundleExecuteInfo` because Metal bakes the push constants into **every pre-encoded ICB command at build time** — by execute time the commands are already written and cannot be patched. D3D12 and Vulkan only read them at execute time (root constants / `vkCmdPushConstants`), so leaving them zeroed at prepare time works perfectly there and hands Metal a bundle full of **zeroed bindless handles**. Fill them identically in both structs; factor the construction into one helper so they cannot drift.

### 3. `commandOffset` / `countIndex` must agree between HLSL and host
The HLSL append call takes both explicitly, and AGFX derives neither:
```hlsl
bundle.DrawIndexed(commandOffset, countIndex, drawId, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
```
The shader reserves a slot with `InterlockedAdd` on `count[countIndex]` and writes at `commandOffset + slot`. Pass the same `commandOffset`/`countIndex` to prepare and execute or the replay reads a different region than the shader filled.

### 4. Per-call `maxCommandCount` bounds a sub-bundle, the count buffer does not
One bundle can back several independent executions sharing the commands buffer (`maxCountCount > 1`) — e.g. one region per material. The caller owns where each region starts and must keep them from overlapping. `agfxIndirectBundleExecuteInfo::maxCommandCount` is the **capacity of that region**, separate from the bundle's creation-time `maxCommandCount`; without it a region at `commandOffset > 0` can read past itself into the next region, since the count clamp bounds only *how many* commands run, not *where* reading stops.

### 5. A bundle shared across frames in flight is a write-after-read hazard
There is one commands buffer, one count buffer, and (on Metal) one ICB per bundle. With `kFramesInFlight > 1`, frame N+1's culling pass can start overwriting them while frame N's draws are still reading them. Either barrier `INDIRECT_ARGUMENT → UNORDERED_ACCESS` before touching the bundle (correct, but serializes culling against the previous frame's draws), or allocate one bundle per frame-in-flight slot (keeps the overlap, costs N× the memory). Symptom of getting this wrong: intermittent flickering geometry that worsens when frame timing changes, e.g. during a window resize.

### 6. `drawID` is the leading field — but on Vulkan `AGFX_DRAW_ID()` does NOT return it
`agfxDrawCommand`/`agfxDrawIndexedCommand`/`agfxDrawMeshCommand` all begin with `uint32_t drawID`, because the D3D12 command signature declares the `CONSTANT` argument (patching root param 1 / `b1`) at index 0 and the terminal draw argument at index 1. `agfxDispatchCommand` has **no** `drawID`: indirect compute is assumed to carry its own addressing (`SV_DispatchThreadID` plus a caller-managed buffer). Read it in the vertex/mesh shader with `AGFX_DRAW_ID()` after declaring `AGFX_DECLARE_DRAW_ID()`.

**The Vulkan divergence:** the command layout is identical (the Vulkan backend strides over the leading `drawID`), but `vkCmdDraw*IndirectCount` has no per-command constant patching, so on Vulkan `AGFX_DRAW_ID()` resolves to the SPIR-V `DrawIndex` builtin — the draw's **linear position in the compacted bundle**, not the `drawId` value the culling shader wrote. On D3D12/Metal the two coincide only by construction of the patch stage. To recover the real drawId portably:

1. The `AGFXIndirectDraw*Bundle` append helpers (`Draw`/`DrawIndexed`/`DrawMesh`) **return the reserved slot**.
2. The producing shader writes `slot → drawId` into its own indirection buffer (a caller-owned RW raw buffer, handle in push constants) under `#if defined(AGFX_VULKAN)`.
3. The consuming vertex/mesh shader resolves `AGFX_DRAW_ID()` through that buffer on Vulkan, and uses it directly elsewhere.

See `culling.hlsl` (producer side) and `gbuffer_indirect.hlsl` (consumer side) for the exact pattern; the host allocates the indirection buffer in `deferred_renderer.cpp::SetupIndirectBundle` and it must be a valid handle on every backend even though only Vulkan reads it. Also: on Vulkan `AGFX_DRAW_ID()` is only valid in vertex/mesh/task stages — forward it to the pixel shader via a `nointerpolation` interpolant.

### 7. Stale commands need no reset pass
Every backend clamps execution to the live count (D3D12's `ExecuteIndirect` and Vulkan's `vkCmdDraw*IndirectCount` via the count buffer, Metal via an `MTLIndirectCommandBufferExecutionRange` the prepare kernel writes). Commands beyond the current count are never executed however stale their contents, so a shrinking draw count frame-to-frame needs no clearing of the commands buffer or ICB. Only the **count** slot must be reset each frame.

## Shader side

Declare the bundle handle as a `uint64_t` push constant (low 32 bits = commands buffer, high 32 = count buffer — `agfxIndirectBundleGetHandle` packs it):

```hlsl
struct CullingPushConstants {
    float4 frustumPlanes[6];
    uint gpuScene;
    uint primitiveCount;
    uint64_t bundleHandle;
    ResourceHandle drawIndirection;   // consumed on Vulkan only -- see gotcha 6
};
AGFX_PUSH_CONSTANTS(CullingPushConstants, g_Constants);

[numthreads(64, 1, 1)]
void main_cs(uint3 dtid : SV_DispatchThreadID)
{
    uint index = dtid.x;
    if (index >= g_Constants.primitiveCount) return;
    if (Culled(index)) return;

    AGFXIndirectDrawIndexedBundle bundle = AGFXIndirectDrawIndexedBundle::Create(g_Constants.bundleHandle);
    uint slot = bundle.DrawIndexed(/*commandOffset*/0, /*countIndex*/0, /*drawId*/index,
                                   inst.indexCount, 1, inst.indexOffset, 0, 0);
#if defined(AGFX_VULKAN)
    // Vulkan's AGFX_DRAW_ID() is the linear slot, not the drawId above -- record slot -> index.
    AGFXRWByteAddressBuffer indirection = AGFXRWByteAddressBuffer::Create(g_Constants.drawIndirection);
    indirection.Store(slot * 4, index);
#endif
}
```

The append is a single atomic reservation plus one contiguous write, and it returns the reserved slot. The consuming vertex/mesh shader then recovers its identity via `AGFX_DRAW_ID()` (resolved through the indirection buffer on Vulkan — gotcha 6) and uses it to index a GPU-scene structured buffer for transforms, material handles, and vertex/index offsets.

## C++ wrapper (`agfx.hpp`)

```cpp
agfx::IndirectBundle bundle = device.CreateIndirectBundle(info);
uint64_t handle = bundle.GetHandle();          // → push constant
agfx::Buffer* cmds = bundle.CommandsBuffer();  // → barriers
computePass.PrepareIndirectBundle(bundle, prepareInfo);
renderPass.ExecuteIndirectBundle(bundle, executeInfo);
```

## ez layer (`agfx_ez.hpp`)

```cpp
ez::IndirectBundle bundle = ctx.CreateIndirectBundle(AGFX_INDIRECT_BUNDLE_TYPE_DRAW_INDEXED, maxDraws);
ctx.TransitionIndirectBundle(bundle, AGFX_RESOURCE_STATE_UNORDERED_ACCESS);
// ... cull + prepare on a raw compute pass via ctx.GetCurrentCommandBuffer() ...
ctx.TransitionIndirectBundle(bundle, AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT);
ctx.ExecuteIndirectBundle(bundle, executeInfo);
```

`TransitionIndirectBundle` moves the commands and count buffers together (they always transition in lockstep) and tracks the state, so it no-ops when already correct. ez has no compute-pass sugar by design: culling, prepare, and DISPATCH-bundle replay all go through `ctx.GetCurrentCommandBuffer().BeginComputePass(...)`. See **using-agfx-ez**.

## Metal backend implementation notes (`agfx_metal4.mm`)

Only relevant when editing the backend itself:

- The four ICB-conversion kernels live as an **inline MSL string** (`kAGFXICBConvertSource`) compiled with `newLibraryWithSource:` at device creation into `agfxDevice::icbConvertPipelines[4]`. `target("agfx")` has no offline shader build step, so the MSC runtime structs the kernels need are restated in the MSL rather than `#include`d — they must stay in sync with `msc_runtime/metal_irconverter_runtime.h` and with the command structs in `agfx.h`. Verify changes with `xcrun metal -c` on the extracted source; a compile failure here only surfaces as an `NSLog` at runtime.
- An `MTLIndirectCommandBuffer` **cannot be a direct kernel buffer argument**. It is reached through an argument-buffer struct holding its `gpuResourceID` (`struct ICBContainer { command_buffer icb; }`), backed by a small persistent buffer written once at bundle creation.
- ICB commands are built with `inheritBuffers = NO` (each command rebinds the descriptor heap, sampler heap, TLAB slice, draw-args slice, and uniforms itself, on every stage that reads them) and `inheritPipelineState = YES` (one pipeline per execute call, set on the encoder).
- Per-command state lives in **persistent** per-bundle buffers (`tlabSlices`, `drawArgsSlices`, `execRanges`), never the per-frame linear allocators — the ICB outlives the encoder that built it. All of them, plus the ICB, need `[residencySet addAllocation:]`.
- The TLAB slice matches `struct agfxTLAB` (128 bytes of push constants followed by `drawID`), which is where Metal Shader Converter expects the `b1` root constant.
- `AGFX_RESOURCE_STATE_INDIRECT_ARGUMENT`'s consumer stage mask includes `MTLStageObject | MTLStageMesh` for DRAW_MESH bundles, not just `MTLStageVertex | MTLStageDispatch`.

## Cross-references
- **agfx-synchronization** — barrier stages, the `agglomerate` flag, encoder-vs-queue barrier scope, fences, frames-in-flight.
- **agfx-writing-bindless-shaders** — `AGFX_PUSH_CONSTANTS`, `AGFX_DRAW_ID`, `AGFXRWByteAddressBuffer`, and the rest of the HLSL side.
- **agfx-render-targets-and-passes** — the render pass the replay happens inside.
- **using-agfx-ez** — the immediate-mode Context wrapping these calls.
