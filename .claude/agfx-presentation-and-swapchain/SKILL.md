---
name: agfx-presentation-and-swapchain
description: ALWAYS use when creating, resizing, or presenting through an agfxSwapChain in AGFX — window/layer handle setup, HDR toggling, vsync, resize handling, or back buffer acquire/present. Trigger for agfxSwapChainCreate/Destroy/Resize/Present, agfxSwapChainAcquireNextTexture, agfxSwapChainGetFormat, AGFX_RESOURCE_STATE_PRESENT, CAMetalLayer/HWND handle, agfxLinuxWindowHandle, agfxDisplayServerProtocol, X11/XCB/Wayland surface setup, "blank window", "swap chain out of date", HDR toggle, resize crash. Do NOT trigger for render pass/attachment authoring against the acquired back buffer texture — use agfx-render-targets-and-passes. Do NOT trigger for fence/frame-in-flight synchronization details — use agfx-synchronization.
---

# AGFX Presentation & Swap Chain

## Overview

`agfxSwapChain` is AGFX's cross-platform presentation object: on macOS it wraps a `CAMetalLayer*`, on Windows an `HWND`, on Linux a pointer to an `agfxLinuxWindowHandle` (a `{display, window}` pair whose interpretation — Xlib, XCB, or Wayland — is fixed by the `agfxDeviceCreateInfo::displayServerProtocol` the device was created with). The contract is deliberately simple — acquire a back buffer texture, render into it via a render pass, barrier it back to `AGFX_RESOURCE_STATE_PRESENT`, submit, then present. AGFX hides the per-backend present sequences (`waitForDrawable`/`signalDrawable` on Metal 4, `IDXGISwapChain::Present` on D3D12, the binary acquire/present semaphore bookkeeping on Vulkan) behind `agfxSwapChainAcquireNextTexture`/`agfxSwapChainPresent`.

Anything that changes the swap chain's underlying resources — resize, or an HDR toggle — requires the GPU to be fully idle first (see `agfx-synchronization` for `drainGPU`). AGFX does not do this drain internally; the caller must drain before resizing or recreating.

## Ownership

**Owns:**
- `agfxSwapChainCreateInfo` / `agfxSwapChainCreate` / `agfxSwapChainDestroy`
- `agfxSwapChainResize`
- `agfxSwapChainGetFormat`
- `agfxSwapChainAcquireNextTexture` / `agfxSwapChainPresent`
- HDR (`isHDR`) and vsync (`vsync`) swap chain configuration
- The native handle contract (`CAMetalLayer*` on macOS, `HWND` on Windows, `agfxLinuxWindowHandle*` on Linux) and the Linux-only `agfxDeviceCreateInfo::displayServerProtocol` field it depends on
- `AGFX_RESOURCE_STATE_PRESENT` as the required pre/post state for the acquired texture

**Doesn't own:**
- Building the render pass that renders into the acquired back buffer texture → `agfx-render-targets-and-passes`
- Draining the GPU / fence semantics that must precede resize or swap chain recreation → `agfx-synchronization`
- Window creation, SDL/AppKit event handling, mouse/keyboard input — engine-level, not part of AGFX

## References

Read `agfx/agfx.h`'s `// Swap chain` section for authoritative struct/enum definitions. `agfx_demo/agfx_demo_main.cpp` is the full reference implementation: swap chain creation from an SDL window, per-frame acquire/present, resize handling, and HDR toggle.

## Design Patterns

### Creating the swap chain

```cpp
agfxSwapChainCreateInfo swapChainCreateInfo = {};
swapChainCreateInfo.queue = graphicsQueue;      // must be a graphics queue
swapChainCreateInfo.imageCount = 2;             // double buffering; 3 for triple buffering
swapChainCreateInfo.width = drawableWidth;
swapChainCreateInfo.height = drawableHeight;
swapChainCreateInfo.isHDR = false;
swapChainCreateInfo.vsync = true;
#if GAME_MAC
swapChainCreateInfo.handle = metalLayer;        // CAMetalLayer* from SDL_Metal_GetLayer or similar
#elif GAME_LINUX
agfxLinuxWindowHandle linuxHandle = {};         // interpreted per the device's displayServerProtocol
linuxHandle.display = x11DisplayOrWlDisplay;    // Display* / xcb_connection_t* / wl_display*
linuxHandle.window  = x11WindowOrWlSurface;     // Window / xcb_window_t / wl_surface* as uint64_t
swapChainCreateInfo.handle = &linuxHandle;      // read during the create call
#else
swapChainCreateInfo.handle = hwnd;
#endif
agfxSwapChain* swapChain = agfxSwapChainCreate(device, &swapChainCreateInfo);
```

On Linux the device must have been created with the matching `agfxDeviceCreateInfo::displayServerProtocol` (`AGFX_DISPLAY_SERVER_PROTOCOL_X11`/`XCB`/`WAYLAND`) — the Vulkan backend enables the corresponding surface extension at instance creation, so this is decided *before* the swap chain exists; a mismatch fails at `agfxSwapChainCreate` with a "was the device created with the right displayServerProtocol?" log. With SDL, pick it from `SDL_GetCurrentVideoDriver()` — see `agfx_demo_main.cpp`. Note `agfx::ez` differs here: raw `agfxSwapChainCreate` consumes the handle struct during the call, but `agfx::ez::Context` stores the pointer (to recreate the swap chain on an HDR toggle), so there it must outlive the Context.

Query the back buffer's actual pixel format with `agfxSwapChainGetFormat(swapChain)` rather than assuming a fixed format — it changes when `isHDR` changes, and any render target/pipeline built against the back buffer must use this queried format (`AGFX_TEXTURE_FORMAT_UNKNOWN` in the render target's `format` field lets it inherit automatically; see `agfx-render-targets-and-passes`).

### Per-frame acquire → render → present

```cpp
agfxTexture* backBuffer = agfxSwapChainAcquireNextTexture(swapChain);
// backBuffer starts in AGFX_RESOURCE_STATE_PRESENT
agfxCommandBufferTextureBarrier(cmd, backBuffer, AGFX_RESOURCE_STATE_PRESENT, AGFX_RESOURCE_STATE_RENDER_TARGET, 0, 0, /*agglomerate=*/false);

// ... wrap backBuffer in an agfxRenderTarget, run a render pass, draw ...

agfxCommandBufferTextureBarrier(cmd, backBuffer, AGFX_RESOURCE_STATE_RENDER_TARGET, AGFX_RESOURCE_STATE_PRESENT, 0, 0, /*agglomerate=*/false);
agfxCommandBufferEnd(cmd);
agfxCommandQueueSubmit(queue, &cmd, 1);
agfxSwapChainPresent(swapChain);
```

Pass `agglomerate = false` specifically for these PRESENT↔RENDER_TARGET transitions around acquire/present (see `agfx-synchronization` for why: presentable drawables need no explicit hazard tracking on Metal, but D3D12 and Vulkan still need the transition emitted unconditionally — on Vulkan it is a real image layout transition to/from `PRESENT_SRC_KHR`, and the backend internally treats PRESENT-as-old-state as `UNDEFINED` since a freshly acquired image's contents are discardable). On Vulkan, `agfxSwapChainAcquireNextTexture` can also return `nullptr` when the surface is out of date (e.g. mid-resize, logged as a warning) — skip the frame and resize rather than dereferencing the result. `agfxSwapChainPresent` must be called only after the command buffer that renders to and barriers the back buffer has already been submitted via `agfxCommandQueueSubmit` — calling it before submit is a synchronization bug.

### Resize handling

Resizing is destructive to in-flight GPU work referencing swap chain resources, so always drain first:

```cpp
// On a window resize event:
drainGPU(); // see agfx-synchronization — signal + wait until the frame fence catches up
agfxSwapChainResize(device, swapChain, newWidth, newHeight);
renderer.Resize(device, newWidth, newHeight); // resize any render targets sized to match the window
```

### HDR toggle

AGFX has no in-place HDR reconfiguration API — toggling HDR means destroying and recreating the swap chain, then recreating anything downstream whose pipeline format depended on the old back buffer format:

```cpp
drainGPU();
agfxSwapChainDestroy(device, swapChain);
swapChainCreateInfo.isHDR = wantHDR;
swapChain = agfxSwapChainCreate(device, &swapChainCreateInfo);
renderer.RecreateTonemapPipeline(device, agfxSwapChainGetFormat(swapChain));
imguiBackend.RecreatePipeline(device, agfxSwapChainGetFormat(swapChain));
```

Skipping the pipeline recreation step is the most common HDR-toggle bug: the render pass will still bind fine, but color-attachment format mismatch between the pipeline and the actual back buffer produces validation errors on D3D12 and garbage/black output on Metal.
