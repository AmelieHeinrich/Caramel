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
