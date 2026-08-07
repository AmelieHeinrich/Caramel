# ReSTIR DI — implementation roadmap

The scaffolding is in. This is the ordered list of what turns it into working ReSTIR.

## What already exists

- `ReSTIRPass` (`Sources/Caramel/Renderer/Passes/ReSTIRPass.{hpp,cpp}`) — four compute dispatches in
  one graph pass, push constants filled, pipelines registered and hot-reload-guarded.
- `scene.restir` (bool) swaps the whole shading path; `DeferredShadingPasses::Enabled` is its
  inverse, so exactly one path writes `sceneLighting` per frame. `scene.restir_ambient` stands in for
  the per-scheme ambient this path has no access to.
- Four stage shaders in `Content/Shaders/`: `ReSTIRInitialSample`, `ReSTIRTemporalReuse`,
  `ReSTIRSpatialReuse`, `ReSTIRShade`. Stages 1–3 resolve the pixel and bail; stage 4 runs a
  brute-force loop identical to `DefaultPBR.hlsl`'s.
- `Content/Shaders/Common/ReSTIR.hlsli` — push-constant mirror, `ReSTIRResolvePixel`,
  `ReSTIRLoadSurface`, `ReSTIRBeginLights` / `ReSTIRLightIndex`, `ReSTIRReproject`, a PCG RNG
  (`ReSTIRSeed` / `ReSTIRRandom`), `ReSTIRWrite`, and a commented-out `Reservoir` sketch.
- `GBufferSurface.hlsli` and `LightList.hlsli` — the gbuffer decode and the cluster light walk,
  shared with the scheme path so the two cannot drift.

**The invariant to protect:** with `scene.restir` on, the image must match the scheme path on a
PBR-only scene. That is the reference every step below gets diffed against. Break it knowingly or not
at all.

---

## Step 1 — Reservoir buffers

**Decide the layout first.** The sketch in `ReSTIR.hlsli` is 16 bytes: `uSampleIndex`, `fWeightSum`,
`uM`, `fW`. That is enough for DI and it keeps the buffer at ~33 MB at 1440p, which is fine. Widen it
only if you find you need the cached target function.

In `ReSTIRPass`:
- Three `agfx::Buffer` + `BufferView` (raw view — `AGFXRWByteAddressBuffer` is what carries the
  `Interlocked*` family, and you will want it eventually). Two ping-ponged by `ctx.frameIndex & 1`
  for temporal, plus one scratch for spatial (see step 4).
- Allocate in `Resize(width, height)`, not per frame — they are per-pixel. Follow
  `ClusteredLightPass::EnsureVisibleLightCapacity` for the growth + `MakeResourcesResident()` shape.
- Point `pc.rReservoirsIn` / `pc.rReservoirsOut` at them; `ReSTIRHasReservoirs()` then goes true and
  the stage bodies start running.
- Add `builder.AlwaysExecute()` to the graph pass. The reservoir buffers are raw agfx objects the
  graph cannot see, so once they carry the pass's real state the reachable-sink evidence is gone —
  same reason `Material Classify` and `Cluster Lights` both declare it.

**Verify:** image unchanged (stage 4 still brute-forces). `RenderGraphPanel` still shows `ReSTIR DI`.

## Step 2 — Initial sampling (RIS)

Fill in `ReSTIRInitialSample.hlsl`. M = 32 candidates is the usual starting point.

- Source pdf is uniform over the light list: `1 / DeferredListTotal`. Draw `j` with `ReSTIRRandom`.
- Target function `p̂` = unshadowed `CookTorrance(...) * radiance`, taken as a luminance scalar.
- `w = p̂ / sourcePdf`; standard reservoir update against `ReSTIRRandom(rng)`.
- Final `fW = fWeightSum / (uM * p̂(chosen))`, guarding `p̂ == 0`.

Then make **stage 4 read the reservoir** instead of looping: one `LightEvaluate` + one
`CookTorrance`, scaled by `fW`. Keep the brute-force loop behind a CVar
(`scene.restir_reference`) — you will want to flip between them constantly.

**Verify:** noisy but *unbiased* — the mean over many static frames must match the reference. A
consistent brightness offset means the `fW` normalisation is wrong, not that it needs more samples.

## Step 3 — Temporal reuse

Fill in `ReSTIRTemporalReuse.hlsl`. `ReSTIRReproject` already gives you last frame's pixel.

- Reject on geometric mismatch before merging: normal dot < ~0.9, relative depth delta > ~10%.
  Motion vectors reproject a *surface*; a reprojected background or a different object is exactly how
  ReSTIR picks up ghosting.
- Clamp history `uM` to ~20× this frame's M, or old samples never wash out and the image lags.
- **Re-evaluate the history sample's `p̂` against this pixel's surface.** The stored one is for the
  previous shading point. Skipping this is the single most common source of a subtly wrong image.

**Verify:** noise collapses dramatically on a static camera. Whip the camera around — smearing on
disocclusions means the rejection test is too loose.

## Step 4 — Spatial reuse

Fill in `ReSTIRSpatialReuse.hlsl`. 5 neighbours in a 30px disk is the usual start.

- Same geometric rejection, same `p̂` re-evaluation as step 3.
- Use a proper MIS weight (Talbot, or pairwise) rather than a flat `1/M`. A flat weight double-counts
  shared samples and the image brightens exactly where reuse is densest.
- **This stage reads and writes the same reservoir set.** Ping-pong into the third buffer from step 1;
  reading a neighbour another thread already overwrote is a correlation bug that looks like blotching,
  not like a race.

**Verify:** the remaining noise floor drops again, especially on surfaces that just disoccluded.

## Step 5 — Shadow rays (what makes it worth doing)

Until this lands, ReSTIR is a slower way to draw the same unshadowed image. The TLAS exists but
nothing consumes it, and there are two real obstacles:

1. **No public accessor.** `AccelerationStructureManager::m_TLAS` is private with no getter and no
   bindless handle. Add both, publish the handle on `FrameContext` (a plain `uint32`, like
   `frameConstantsHandle`), and add `rTlas` to the push constants. `AGFXRaytracingAccelerationStructure`
   in `AGFX.hlsli` is the shader-side type; use an inline `RayQuery` with
   `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` for visibility.
2. **Queue hazard.** `AccelStructPass` runs on the async compute queue and is submitted with no
   GPU-side ordering against the graphics queue (`Renderer::Render`), and `RecordTLASBuild` rewrites
   the TLAS in place every frame. Reading it from a graphics pass in the same frame races the
   rebuild. Pick one: move the build to the graphics queue while developing, add a cross-queue
   semaphore wait before shading, or double-buffer the TLAS. Do **not** just ignore it — it will
   present as intermittent corruption on one platform only.

Remember `render.build_acceleration_structures` defaults to **false**; ReSTIR should either force it
or refuse to enable shadow rays without it.

Where the ray goes: one visibility ray for the survivor at the end of initial sampling, zeroing `fW`
on occlusion. That is what makes the reservoir carry shadowing through temporal and spatial reuse for
free, which is the entire point of the algorithm.

**Verify:** real shadows from every light at roughly the cost of one ray per pixel. This is the
milestone where ReSTIR should beat the scheme path outright on `ReSTIR_ClusteredTest.cscene`.

---

## Loose ends, once it works

- **Material schemes.** This path hardcodes PBR, so a `Toon` surface shades wrong while it is on.
  Either fold the shade stage back into the per-scheme indirect dispatch (the reservoir stages stay
  standalone; only stage 4 moves) or accept it. Note the Metal constraint: push constants are baked
  into the ICB at `PrepareIndirectBundle` time, so anything the scheme kernels read must be final
  before `Material Classify` registers.
- **`scene.restir_ambient`** is a placeholder for the per-material ambient the scheme path reads out
  of `DefaultPBR.json`. It goes away if the shade stage moves back into the scheme dispatch.
- **Light sampling.** Uniform over the cluster list is fine to start, but a power/distance-weighted
  pdf cuts variance a lot for free.
- **Denoising.** ReSTIR DI still wants a spatial denoiser. This is where TAA / MetalFX temporal on
  `Notes/TODO.md` stops being optional.
- **Overflow.** `kMaxLightsPerCluster` is 128. `scene.gbuffer_debug 10` is the heatmap;
  `ClusteredLightPass::ReportOverflow` warns once. ReSTIR does not change that ceiling — it just
  makes exceeding it cheaper to shade through.
