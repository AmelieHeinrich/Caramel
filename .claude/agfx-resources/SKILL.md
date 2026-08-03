---
name: agfx-resources
description: ALWAYS use when creating, uploading to, aliasing, or destroying AGFX textures and buffers — including placement heaps and the resource lifetime/residency rules. Trigger for agfxTextureCreate/Destroy/GetInfo/SetName/ReplaceRegion, agfxBufferCreate/Destroy/Map/Unmap/SetName, agfxTextureViewCreate/agfxBufferViewCreate, agfxTextureUsage/agfxBufferUsage/agfxBufferMemoryType, AGFX_BUFFER_MEMORY_TYPE_UPLOAD/READBACK/GPU_ONLY, agfxDeviceMakeResourcesResident, agfxHeapCreate/Destroy, agfxHeapCreateInfo, agfxAllocationInfo, agfxDeviceGetTextureAllocationInfo/GetBufferAllocationInfo, heap/heapOffset on a create info, agfxCommandBufferAliasingBarrier, agfxComputePassCopyBufferToTexture/CopyBufferToBuffer/CopyTextureToBuffer, "staging buffer", "upload texture", "readback", "resource aliasing", "placement heap", "transient render target", "memory reuse", "GPU fault on resource", "resource not resident". Do NOT trigger for fence/frame-pacing or general barrier-and-agglomerate semantics — use agfx-synchronization. Do NOT trigger for render target/attachment authoring — use agfx-render-targets-and-passes. Do NOT trigger for how a shader consumes a view handle — use agfx-writing-bindless-shaders.
---

# AGFX Resources: Creation, Lifetime, Heaps & Aliasing

## Overview

AGFX resources come in two layers that are easy to conflate: the **allocation** (`agfxTexture`, `agfxBuffer`) and the **view** (`agfxTextureView`, `agfxBufferView`, `agfxSampler`). The allocation owns memory; the view owns the bindless descriptor a shader actually indexes. Nothing is bound by slot — a shader reaches a resource only through the `uint64_t` handle from `agfxTextureViewGetHandle`/`agfxBufferViewGetHandle`, passed in via push constants.

Three rules cause most resource bugs in AGFX, and all three are invisible on some backends while fatal on another:

1. **Usage flags are a contract, not a hint.** A texture without `AGFX_TEXTURE_USAGE_STORAGE` cannot get a writeable view; a buffer without `AGFX_BUFFER_USAGE_SHADER_WRITE` cannot get a writeable one. D3D12 tends to tolerate over-broad flags; Metal derives its `MTLTextureUsage` (and Vulkan its `VkImageUsageFlags`) directly from them and produces a black texture or a validation failure.
2. **Residency is explicit.** Creating a resource does not make it usable. `agfxDeviceMakeResourcesResident` must be called after creation and before the first submit that touches it — on Metal this commits the device residency set, and a resource missing from it is a GPU fault, not a warning. It is a no-op on D3D12 and Vulkan, so forgetting it is a Metal-only crash.
3. **Destruction is not deferred.** No AGFX object retains a resource for you, and command buffers do not either. Destroying a resource an in-flight command buffer still references is a use-after-free. Drain first — see `agfx-synchronization`.

## Ownership

**Owns:**
- `agfxTextureCreate`/`Destroy`/`GetInfo`/`SetName`/`ReplaceRegion`, `agfxTextureUsage`, `agfxTextureType`
- `agfxBufferCreate`/`Destroy`/`Map`/`Unmap`/`GetInfo`/`SetName`, `agfxBufferUsage`, `agfxBufferMemoryType`
- `agfxTextureViewCreate`/`agfxBufferViewCreate` and their bindless handles (the *creation* side)
- `agfxDeviceMakeResourcesResident` and the residency model
- Staging-buffer upload and readback patterns, `agfxComputePassCopyBufferToTexture` and friends
- `agfxHeap`, `agfxHeapCreateInfo`, `agfxAllocationInfo`, the allocation-info queries, `heap`/`heapOffset` placement
- Resource aliasing: heap layout, lifetime overlap rules, and `agfxCommandBufferAliasingBarrier`'s *usage*
- Resource destruction ordering (views before resources, resources before heaps)

**Doesn't own:**
- Fences, frames-in-flight pacing, the `agglomerate` flag's general semantics, UAV-vs-transition barrier choice → `agfx-synchronization` (the aliasing barrier is documented here because it is meaningless outside a heap, but it obeys every rule in that skill)
- Wrapping a texture as an attachment, load/store ops → `agfx-render-targets-and-passes`
- How a shader declares and indexes a handle (`ResourceHandle`, `AGFX_PUSH_CONSTANTS`) → `agfx-writing-bindless-shaders`
- Acceleration structure buffers → `agfx-raytracing`
- The `ez` layer's `CreateTexture2D`/`CreateStructuredBuffer` sugar and its cached views → `using-agfx-ez`

## References

`agfx/agfx.h` is authoritative — the doc comments on `agfxBufferMemoryType`, `agfxTextureCreateInfo::heap`, and `agfxCommandBufferAliasingBarrier` carry the constraints that matter. `agfx_tests/tests/test_aliasing.cpp` is the reference heap/aliasing implementation across all three API flavours (C, C++, ez); `agfx_tests/test_gpu.cpp`'s `UploadBuffer`/`ReadbackBuffer` are the reference staging paths.

## Design Patterns

### Creating a texture and its view

The view is a separate object with its own lifetime, and it is what carries the bindless handle:

```cpp
agfxTextureCreateInfo texInfo{};
texInfo.type = AGFX_TEXTURE_TYPE_2D;
texInfo.format = AGFX_TEXTURE_FORMAT_RGBA16F;
texInfo.usage = AGFX_TEXTURE_USAGE_STORAGE | AGFX_TEXTURE_USAGE_SAMPLED;
texInfo.width = width;
texInfo.height = height;
texInfo.depthOrArrayLayers = 1;
texInfo.mipLevels = 1;
agfxTexture* texture = agfxTextureCreate(device, &texInfo);

agfxTextureViewCreateInfo viewInfo{};
viewInfo.texture = texture;
viewInfo.format = texInfo.format;
viewInfo.type = texInfo.type;
viewInfo.mipLevelCount = 1;
viewInfo.arrayLayerCount = 1;
viewInfo.writeable = true;            // UAV/storage view; requires USAGE_STORAGE above
agfxTextureView* uav = agfxTextureViewCreate(device, &viewInfo);

agfxDeviceMakeResourcesResident(device);  // before the first submit that touches it

constants.myTexture = (uint32_t)agfxTextureViewGetHandle(uav);
```

One texture can carry several views (a read-only view and a writeable one, per-mip views for a mip chain, a single-layer view of an array). Create them at setup, not per frame.

### Memory types and the staging upload

`agfxBufferMemoryType` decides whether `agfxBufferMap` works at all:

| Memory type | Mappable | Use for |
|---|---|---|
| `GPU_ONLY` / `DEFAULT` | No | Everything the GPU alone touches: vertex/index, structured, UAV targets |
| `CPU_TO_GPU` / `UPLOAD` | Yes (write) | Staging sources, per-frame constants written by the CPU |
| `GPU_TO_CPU` / `READBACK` | Yes (read) | Readback destinations |

Mapping a `GPU_ONLY` buffer is invalid. The upload path for GPU-only data is always staging buffer → copy:

```cpp
agfxBufferCreateInfo stagingInfo{};
stagingInfo.size = size;
stagingInfo.memoryType = AGFX_BUFFER_MEMORY_TYPE_UPLOAD;
agfxBuffer* staging = agfxBufferCreate(device, &stagingInfo);
agfxDeviceMakeResourcesResident(device);

memcpy(agfxBufferMap(staging), data, size);
agfxBufferUnmap(staging);

// Copies are recorded in a compute pass on every backend.
agfxComputePass* pass = agfxComputePassBegin(cmd, "upload");
agfxComputePassCopyBufferToBuffer(pass, staging, dst, 0, 0, size);
agfxComputePassEnd(pass);
// ... submit, then wait before destroying `staging`.
```

Texture uploads work the same way through `agfxComputePassCopyBufferToTexture`. `agfxTextureReplaceRegion` exists as a direct CPU write, but it only works for committed, CPU-visible textures — **it silently does nothing for a heap-placed texture**, which is `MTLStorageModePrivate` on Metal and `D3D12_HEAP_TYPE_DEFAULT` on D3D12. Placed textures must be filled by the GPU: a copy pass, or a full compute/render overwrite.

Bracket copies with the usual state transitions (`COPY_DEST`/`COPY_SOURCE`) per `agfx-synchronization`.

### Destruction order

Destruction is strictly inside-out, and nothing is refcounted:

```
views  →  textures/buffers  →  heaps
```

`agfxHeapDestroy`'s contract is explicit: resources placed in a heap do **not** keep it alive. Destroying the heap first leaves every placed resource dangling. And before any of it, drain the GPU if in-flight work might still reference the resource.

## Resource Aliasing

### When it is worth it

Aliasing places two resources at overlapping offsets in one `agfxHeap` so they share physical memory. It pays off when a frame has several large transients whose lifetimes **do not overlap** — an SSAO target, a bloom chain, a DoF buffer, each live for two or three passes. A render graph that would otherwise allocate peak-sum memory allocates peak-concurrent instead.

It is not free: you take over placement, alignment and hazard tracking. Don't alias resources that are live simultaneously, and don't alias small resources — the bookkeeping outweighs the saving.

### Sizing the heap and placing resources

Alignment is backend-specific and must never be hardcoded. Query it for the *exact* create info you will pass to create, then round each offset up:

```cpp
agfxAllocationInfo alloc{};
agfxDeviceGetTextureAllocationInfo(device, &transientInfo, &alloc);
// alloc.size = bytes needed, alloc.alignment = the offset must be a multiple of this

agfxHeapCreateInfo heapInfo{};
heapInfo.size = (alloc.size + alloc.alignment - 1) & ~(alloc.alignment - 1);
heapInfo.memoryType = AGFX_BUFFER_MEMORY_TYPE_GPU_ONLY;  // required for textures
agfxHeap* heap = agfxHeapCreate(device, &heapInfo);
if (!heap) { /* tier-1 D3D12 adapter, or out of memory -- fall back to committed */ }

// Three transients sharing one slot: same offset, non-overlapping lifetimes.
agfxTextureCreateInfo t0 = transientInfo; t0.heap = heap; t0.heapOffset = 0;
agfxTextureCreateInfo t1 = transientInfo; t1.heap = heap; t1.heapOffset = 0;
agfxTexture* a = agfxTextureCreate(device, &t0);
agfxTexture* b = agfxTextureCreate(device, &t1);

agfxDeviceMakeResourcesResident(device);
```

Constraints worth knowing up front:

- **Textures may only be placed in `AGFX_BUFFER_MEMORY_TYPE_GPU_ONLY` heaps.** Both backends enforce this.
- **A buffer's `memoryType` must equal its heap's.** Metal requires the storage modes to match; the Metal backend rejects a mismatch rather than silently picking one.
- `agfxHeapCreate` returns `nullptr` on a D3D12 adapter reporting resource heap tier 1. Every Metal 4 device supports placement heaps, and the Vulkan backend has no tier gate either (a heap is one `VkDeviceMemory` allocation; creation only fails when no device memory type satisfies the request, or on OOM). Always handle the `nullptr` and fall back to committed allocations.
- The allocation-info queries return `{0, 0}` on failure — the natural place to detect an unsupported device early.

### The aliasing barrier

This is the part that is *not* an optimization. Aliased resources are different handles with no shared subresource, so no usage-derived dependency tracker will ever emit an edge between them. The barrier **is** that edge: without it, the outgoing resource's last writer and the incoming resource's first user can overlap and corrupt each other's memory.

```cpp
// Textures: the dedicated entry point. Only the incoming texture is named -- the outgoing one
// contributes its state, not a pointer, since it may already be destroyed by now.
agfxCommandBufferAliasingBarrier(cmd, incomingTexture,
    /*outgoingState*/ AGFX_RESOURCE_STATE_UNORDERED_ACCESS,
    /*incomingState*/ AGFX_RESOURCE_STATE_UNORDERED_ACCESS,
    /*agglomerate*/   true);

// Buffers and acceleration structures: a plain memory barrier. There is deliberately no buffer
// aliasing entry point -- a resource-less memory barrier has no layout to misinterpret.
agfxCommandBufferMemoryBarrier(cmd, outgoingState, incomingState, /*agglomerate*/ true);
```

Three rules:

- **`outgoingState` must be the honest previous state** of whatever last used that memory — it is what the GPU is actually made to wait on. Passing `COMMON` because it is convenient discards the wait.
- **`agglomerate` must be `true`.** On Metal, `false` makes the barrier a silent no-op, which reintroduces exactly the corruption the barrier exists to prevent. D3D12 ignores the flag, so a wrong value here is a Metal-only, timing-dependent bug.
- **The incoming resource's contents are undefined.** Initialize before reading: `AGFX_LOAD_OPERATION_CLEAR`/`DONT_CARE` on the first render pass, or a full compute overwrite. Do not assume the outgoing resource's bytes survive — D3D12 formally leaves aliased *texture* contents undefined even when the memory physically persists.

### Backend notes (if editing a backend)

- **D3D12** requires resource heap tier 2, checked once at device creation into `agfxDevice::supportsPlacementHeaps`; placed resources use `CreatePlacedResource`. Enhanced Barriers has no aliasing barrier type, so `agfxCommandBufferAliasingBarrier` emits *two* groups: a global barrier carrying the outgoing state's real sync+access scopes (the only thing that flushes the outgoing writes), and a texture barrier with `LAYOUT_UNDEFINED` → the incoming layout and `BARRIER_FLAG_DISCARD`.
- **Vulkan** backs a heap with a single `VkDeviceMemory` allocation (flagged for device addresses); placed resources bind into it at their `heapOffset`. `agfxCommandBufferAliasingBarrier` has the same two-part shape as D3D12's: a global `VkMemoryBarrier2` carrying the outgoing state's real scopes (the flush), plus a `VkImageMemoryBarrier2` with `oldLayout = UNDEFINED` activating the incoming image — Vulkan just carries both in one `VkDependencyInfo`.
- **Metal** uses `MTLHeapTypePlacement` (not `Automatic`, which picks its own offsets). The heap, and every resource placed in it, must agree on storage mode, CPU cache mode and hazard tracking mode — all derived from the heap's `memoryType`, with `MTLHazardTrackingModeUntracked` since AGFX synchronizes explicitly. **The heap is added to the residency set once; placed resources deliberately skip the per-resource `addAllocation`**, because making a heap resident makes everything in it resident. Sizes and alignments come from `heapTextureSizeAndAlignWithDescriptor:`/`heapBufferSizeAndAlignWithLength:options:`, which must be queried with the exact descriptor creation will use or the reported alignment is wrong. The aliasing barrier needs no special path: the stage tracker is already resource-agnostic and already emits `MTL4VisibilityOptionDevice`, which is what Apple prescribes for ordinary placement-heap aliasing.

## Common Mistakes

- **Forgetting `agfxDeviceMakeResourcesResident` after creating resources.** No-op on D3D12 and Vulkan, GPU fault on Metal. Call it once after a batch of creations, not per resource, and never per frame.
- **Requesting a writeable view without the matching usage flag** (`AGFX_TEXTURE_USAGE_STORAGE` / `AGFX_BUFFER_USAGE_SHADER_WRITE`). Produces a black texture or a validation error rather than an obvious failure at create time.
- **Mapping a `GPU_ONLY` buffer.** Only `UPLOAD` and `READBACK` are mappable; go through a staging buffer.
- **`agfxTextureReplaceRegion` on a heap-placed texture.** Placed textures are GPU-private on every backend. Use a copy pass.
- **Hardcoding a heap offset or alignment.** Alignment differs by backend and by create info; an offset derived from one backend's numbers is not valid on another. Always go through the allocation-info query, and re-query if the create info changes.
- **Destroying a heap before the resources placed in it**, or a resource before its views. Nothing is refcounted.
- **Destroying any resource without draining first** when in-flight work may reference it.
- **Aliasing without the barrier, or with `agglomerate = false`.** Runs correctly on D3D12 and Vulkan (which ignore the flag) and corrupts on Metal — the worst possible failure shape.
- **Assuming aliased memory carries data across the alias.** Contents are undefined; initialize the incoming resource.

## Verification

The aliasing tests are the executable spec — `xmake run agfx_tests --filter Alias` runs nine (three scenarios × C/C++/ez). `AliasHeapTransients` is golden-compared, and the golden is byte-identical across backends, so a cross-backend mismatch there is a real backend divergence rather than a tolerance issue. Run resource work under the Metal validation layer (`MTL_DEBUG_LAYER=1`); placement-heap mode mismatches, bad offsets and non-resident resources all surface there with precise diagnostics, and none of them are visible on D3D12. On Linux, `agfxDeviceCreateInfo::enableValidation` enables `VK_LAYER_KHRONOS_validation` when it is installed (the backend logs a warning if it isn't — validation is silently absent then).
