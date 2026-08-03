Basics
- Cleanup
- Simple scripting
- Render graph
- Material bucketing (render queues)
- GPU scene representation
- BLAS/TLAS building (async compute)

GPU driven rendering
- Indirect mesh draw setup
- Frustum culling (instance, meshlet)
- Cone culling (meshlet)
- Sub-pixel culling (primitive)
- HW/SW rasterizer
- 2-pass occlusion cull

Base lighting pass
- Cook-torrance + burley
- Clustered light culling
- IES profiles

Temporal infrastructure
- Camera jitter + 2.5D MVs
- DLSS/FSR/MetalFX

Pick and choose:
- CSM
- VSM
- RT soft shadows
- DDGI
- ReSTIR GI
- Pathtracer
- Volumetric clouds/god rays
- Bloom
- TAA
- DOF
- Motion blur
- Auto exposure
- Compute particles
- RTAO
- SSRT reflections
- VRT
- Texture streaming
- Vegetation/foliage
- HDR10/scRGB output
- CAS
- Simple animation
- Decals
- Contact shadows
- Physically based camera
- ACES 2.0 tonemapper
- GPU raytraced audio
- FFT ocean
- LTC area lights
- Baked reflection/GI probes
- Subsurface scattering
- Anisotropic specular
- Hair rendering
- Sreen-space caustics
- Moment-based OIT