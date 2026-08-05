# Caramel : research renderer powered by AGFX

Caramel is an experimental research renderer powered by [AGFX](https://github.com/AmelieHeinrich/agfx), a modern D3D12/Vulkan/Metal 4 RHI.

## Features

- Intuitive editor with mouse picking via Jolt Physics
- Scripting capabilities (AngelScript)
- Scene system with serialization
- Heavily multi-threaded asset system and asset streaming for textures and meshes via copy queue
- Custom engine format for meshes and textures
- Multiple material support with binning
- Cook-Torrance BRDF + Burley diffuse
- Render graph with transient resource allocator
- GPU driven mesh shaded visibility buffer with instance frustum/occlusion cull + LOD (cross fade dither + hysteresis), cluster cone/contribution/frustum/occlusion cull, primitive frustum/occlusion cull

## Planned
- Clustered light culling
- IES profiles
- Simple animation playback with bounding volume update for mesh culling pass
- DLSS/FSR/MetalFX support
- ReSTIR DI
- DDGI
- SSRT reflections
- ReSTIR GI
- HDR output with luminosity heatmap and CIE diagram visualizer
- Reference pathtracer
- Moment-based OIT
- NRD integration
- RTAO
- RT soft shadows
- Cascaded shadow maps
- VSMs?
- Auto-exposure
- Motion blur
- Upsample/downsample bloom
- Deferred decals
- Physically based camera
- Volumetric clouds
- Temporal anti-aliasing
