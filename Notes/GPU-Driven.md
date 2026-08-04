Basics
- Dump everything into an indirect command buffer
- GPU-side prefix sum/parallel scan

Discrete LOD
- Per instance LOD

Culling
- Instance frustum cull
- Meshlet frustum + cone cull
- Two-pass HZB occlusion cull

Visibility
- R32G32 uint, write instance, meshlet, prim id

GBuffer
- CalcFullBary + InterpolateWithDeriv
- Mip selection
- GBuffer

Multi shading model
- Classify materials: count -> prefix sum -> scatter -> build indirect args
- Dispatch indirect, get a clean deferred shaded output

Temporal stability
- Implement TAA
