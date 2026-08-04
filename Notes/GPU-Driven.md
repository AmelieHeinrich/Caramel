Basics
- GPU-side prefix sum/parallel scan
- Dump everything into an indirect command buffer

Culling
- Instance frustum cull
- Meshlet frustum + cone cull
- Two-pass HZB occlusion cull

Visibility
- R32G32 uint, write instance, meshlet, prim id
- SW rasterizer, route cluster if below pixel size
- Compare triangle density

GBuffer
- CalcFullBary + InterpolateWithDeriv
- Mip selection
- GBuffer

Multi shading model
- Classify materials: count -> prefix sum -> scatter -> build indirect args
- Dispatch indirect, get a clean deferred shaded output

Discrete LOD
- Per instance LOD
- Per meshlet LOD

Temporal stability
- Implement TAA
