# 🍮 Caramel

An experimental research renderer powered by [AGFX](https://github.com/AmelieHeinrich/agfx), a modern D3D12 / Vulkan / Metal 4 RHI.

![Visibility buffer stress test — 10 billion triangles](.github/Visbuffer.png)

## Features

### Rendering
- GPU-driven, mesh-shaded visibility buffer
- Instance frustum & occlusion culling
- Cluster cone / contribution / frustum / occlusion culling
- LOD selection with cross-fade dithering and hysteresis
- Cook-Torrance BRDF with Burley diffuse
- Multiple material support with binning
- Render graph with transient resource allocator

### Engine
- Windows, Linux, and macOS support (D3D12, Vulkan, Metal 4)
- Intuitive editor with mouse picking via Jolt Physics
- Scripting via AngelScript
- Scene system with serialization
- Heavily multi-threaded asset system with copy-queue streaming for textures and meshes
- Custom engine formats for meshes and textures

## Debug Views

| Meshlets | Primitives |
| :---: | :---: |
| ![Meshlet ID view](.github/VisbufferMeshlets.png) | ![Primitive ID view](.github/VisbufferPrimitives.png) |

## Building

Caramel uses [xmake](https://xmake.io):

```sh
xmake        # build everything
xmake run Caramel
```

## Roadmap

### Lighting & Shadows
- Clustered light culling
- IES profiles
- Cascaded shadow maps, VSMs (maybe), RT soft shadows

### Global Illumination & Ray Tracing
- ReSTIR DI / ReSTIR GI
- DDGI
- SSRT reflections
- RTAO
- NRD integration
- Reference pathtracer

### Post Processing & Display
- Temporal anti-aliasing
- DLSS / FSR / MetalFX support
- HDR output with luminosity heatmap and CIE diagram visualizer
- Auto-exposure
- Motion blur
- Upsample/downsample bloom
- Physically based camera

### Misc
- Simple animation playback with bounding volume updates for the culling pass
- Moment-based OIT
- Deferred decals
- Volumetric clouds

See [Notes/TODO.md](Notes/TODO.md) for the full wishlist.
