/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Populates the Opaque indirect bundle: one DrawMesh command per resident scene instance that
// survives culling (two while the instance cross-fades between LODs). Dispatched twice per frame,
// once per pass of the two-pass occlusion scheme -- see the uPass comment below.

#include "Common/AGFX.hlsli"
#include "Common/GPUScene.hlsli"
#include "Common/HZB.hlsli"

#pragma compute PopulateOpaqueIndirectBundleCS

struct FrameConstants {
    float4x4 mView;
    float4x4 mProjection;
    float4x4 mViewProjection;
    float4x4 mInvView;
    float4x4 mInvProjection;
    float4x4 mInvViewProjection;
    float4   vFrustumPlanes[6]; // Left/Right/Bottom/Top/Near/Far, xyz = normal, w = distance
    float3   vCameraPosition;
    float    fNearPlane;
    float    fFarPlane;
    float3   _Pad;
    float4x4 mHZBViewProjection; // the camera the current pyramid was rasterized from
};

struct PopulatePushConstants {
    ResourceHandle rInstanceBuffer;
    uint           uInstanceCount;
    uint64_t       uBundleHandle;
    ResourceHandle rDrawIndirection; // valid on every backend; only read/written on Vulkan
    ResourceHandle rFrameConstants;
    ResourceHandle rInstanceLodTable;    // GPULodInfo per (instance, lod) -- see GPUScene.hlsli
    ResourceHandle rLodStateBuffer;      // uint per state slot, persists across frames (layout below)
    float          fLodBaseDistance;     // first LOD boundary distance
    float          fLodDistanceMultiplier; // geometric falloff per successive boundary
    ResourceHandle rMaterialBuffer;      // GPUMaterial per slot; read for the doubleSided flag
    uint           uRegionCapacity;      // commands per bundle region

    // 0 = early pass (redraw what was visible last frame), 1 = late pass (everything that is visible
    // now against the freshly built pyramid, minus whatever the early pass already drew).
    uint           uPass;
    ResourceHandle rVisibilityBuffer;    // uint per state slot, persists across frames
    uint           uCullFlags;           // bit0 = culling enabled, bit1 = the pyramid holds real data
    ResourceHandle rHZB;
    uint           uHZBWidth;   // scalars, not a uint2: a uint2 may not straddle a 16-byte boundary
    uint           uHZBHeight;  // under cbuffer packing, so adding a field above silently shifts it
    uint           uHZBMipCount;
    float          fLodHysteresis;       // relative distance dead-band around each LOD boundary
    uint           uLodFadeStep;         // fade progress added per frame; kLodStateFadeMax = a full fade
};
AGFX_PUSH_CONSTANTS(PopulatePushConstants, g_Constants);

static const uint kPopulatePassEarly = 0;
static const uint kPopulatePassLate = 1;

static const uint kCullFlagEnabled = 1u;
static const uint kCullFlagHZBValid = 2u;
// Clear on the first frame, after a resize, and after the visibility buffers are reallocated: the
// flags describe an instance set or a projection that no longer exists. The early pass then draws
// nothing and zeroes them, and the late pass rebuilds them from scratch against the fresh pyramid.
static const uint kCullFlagVisibilityValid = 4u;

// Instance visibility word layout, shared with SceneMesh.hlsli's SceneAS (which reads bits 1/2 to
// decide what the early pass already drew):
//  bit 0: passed the late pass's instance tests -- next frame's early pass redraws it
//  bit 1: drawn by this frame's early pass
//  bit 2: the meshlet visibility bits describe this frame's selected LOD
//  bits 8-15: (LOD the meshlet bits were last written for) + 1, 0 = never written
static const uint kInstanceVisVisible = 1u;
static const uint kInstanceVisDrawnEarly = 2u;
static const uint kInstanceVisBitsLodValid = 4u;
static const uint kInstanceVisLodShift = 8;
static const uint kInstanceVisLodMask = 0xFF00u;

// Transforming just the min/max corners through a rotated instance transform does not yield the
// world-space AABB (it only forms a valid box for axis-aligned/translation-only transforms) --
// InstanceSpawner.as's randomRotation option produces exactly the rotated transforms that broke
// this. Re-derive the world AABB from all 8 local corners instead.
void ComputeWorldBounds(GPUInstance instance, out float3 worldMin, out float3 worldMax)
{
    float3 localMin = instance.vBoundsMin.xyz;
    float3 localMax = instance.vBoundsMax.xyz;

    worldMin = float3(3.402823466e+38, 3.402823466e+38, 3.402823466e+38);
    worldMax = -worldMin;

    [unroll]
    for (int corner = 0; corner < 8; ++corner) {
        float3 localCorner = float3(
            (corner & 1) ? localMax.x : localMin.x,
            (corner & 2) ? localMax.y : localMin.y,
            (corner & 4) ? localMax.z : localMin.z);
        float3 worldCorner = mul(instance.mTransform, float4(localCorner, 1.0)).xyz;
        worldMin = min(worldMin, worldCorner);
        worldMax = max(worldMax, worldCorner);
    }
}

bool IsInstanceVisible(float3 worldMin, float3 worldMax, FrameConstants frame)
{
    [unroll]
    for (int plane = 0; plane < 6; ++plane) {
        float4 frustumPlane = frame.vFrustumPlanes[plane];
        float3 normal = frustumPlane.xyz;
        float distance = frustumPlane.w;

        // Compute the positive vertex of the AABB for this plane.
        float3 positiveVertex = worldMin;
        if (normal.x >= 0) positiveVertex.x = worldMax.x;
        if (normal.y >= 0) positiveVertex.y = worldMax.y;
        if (normal.z >= 0) positiveVertex.z = worldMax.z;

        // If the positive vertex is outside the frustum plane, the AABB is not visible
        if (dot(normal, positiveVertex) + distance < 0) {
            return false;
        }
    }
    return true;
}

// Per-instance LOD state word, in rLodStateBuffer, persists across frames (same lifecycle and
// validity guard as the visibility buffer -- kCullFlagVisibilityValid):
//  bits 0-3 : committed LOD + 1, 0 = uninitialized -> snap to the desired LOD without fading
//  bits 4-7 : outgoing LOD + 1, 0 = not fading
//  bits 8-23: fade progress, 0..kLodStateFadeMax. Advanced by uLodFadeStep per frame, which the CPU
//             derives from real delta time so the fade lasts the same wall-clock duration at any
//             frame rate (a frame counter reads as an instant pop at high FPS).
// Advanced exactly once per frame by the early pass (for every instance, visible or not, so fades
// finish off-screen); the late pass loads it read-only, so both passes agree within a frame and the
// one-LOD-per-instance assumption behind kInstanceVisBitsLodValid holds.
static const uint kLodStateCommittedMask = 0xFu;
static const uint kLodStateOutgoingShift = 4;
static const uint kLodStateFadeShift = 8;
static const uint kLodStateFadeMax = 0xFFFFu;

// Distance-threshold LOD ladder: starts at the finest LOD and drops one level for every boundary
// the distance exceeds; boundary b separates LOD (kLodCount-1-b) from LOD (kLodCount-2-b), at
// threshold fLodBaseDistance * fLodDistanceMultiplier^b.
uint LodFromDistance(float distance)
{
    uint lod = kLodCount - 1;
    [unroll]
    for (uint b = 0; b < kLodCount - 1; ++b) {
        float threshold = g_Constants.fLodBaseDistance * pow(g_Constants.fLodDistanceMultiplier, (float)b);
        if (distance > threshold)
            lod = min(lod, kLodCount - 2 - b);
    }
    return lod;
}

uint AdvanceLodState(uint slot, GPUInstance instance, FrameConstants frame, float3 worldMin, float3 worldMax, bool stateValid)
{
    AGFXRWStructuredBuffer<uint> lodStates = AGFXRWStructuredBuffer<uint>::Create(g_Constants.rLodStateBuffer);
    uint state = stateValid ? lodStates.Load(slot) : 0u;

    float distance = length((worldMin + worldMax) * 0.5f - frame.vCameraPosition);
    uint committed = state & kLodStateCommittedMask;
    uint outgoing = (state >> kLodStateOutgoingShift) & 0xFu;
    uint fadeProgress = (state >> kLodStateFadeShift) & kLodStateFadeMax;

    if (committed == 0 || committed - 1 > instance.uLod) {
        // First sight of this instance, or the residency ceiling dropped below the committed LOD
        // (that data is gone) -- snap, no fade.
        state = min(LodFromDistance(distance), instance.uLod) + 1;
    } else if (outgoing != 0) {
        // Mid-fade: just advance. Retargets wait until the fade completes, which together with the
        // dead-band below prevents thrash; a multi-level jump fades in one go.
        fadeProgress += g_Constants.uLodFadeStep;
        state = fadeProgress >= kLodStateFadeMax
              ? committed
              : committed | (outgoing << kLodStateOutgoingShift) | (fadeProgress << kLodStateFadeShift);
    } else {
        // Hysteresis: the committed LOD sticks while it stays inside the dead-band [lo, hi] formed
        // by biasing the distance both ways; only a crossing past the band starts a fade.
        uint lo = LodFromDistance(distance * (1.0f + g_Constants.fLodHysteresis));
        uint hi = LodFromDistance(distance * (1.0f - g_Constants.fLodHysteresis));
        uint target = min(clamp(committed - 1, lo, hi), instance.uLod) + 1;
        if (target != committed)
            state = target | (committed << kLodStateOutgoingShift);
    }

    lodStates.Store(slot, state);
    return state;
}

uint LodStateFadeNibble(uint state)
{
    uint outgoing = (state >> kLodStateOutgoingShift) & 0xFu;
    if (outgoing == 0)
        return kDrawWordFadeOpaque;
    uint fadeProgress = (state >> kLodStateFadeShift) & kLodStateFadeMax;
    return min(fadeProgress * kDrawWordFadeOpaque / kLodStateFadeMax, kDrawWordFadeOpaque);
}

// Appends one draw of the given LOD into the region matching this pass and this material's
// sidedness. A cross-fading instance gets two of these per late pass: the committed LOD (incoming)
// and the outgoing LOD, distinguished by the draw word.
void EmitDraw(uint index, GPUInstance instance, uint lod, uint fade, bool outgoing)
{
    GPULodInfo lodInfo = SceneLoadLodInfo(g_Constants.rInstanceLodTable, index, lod);

    // The consuming pipeline has a task shader (SceneMesh.hlsli's SceneAS) in front of the mesh
    // shader, so this group count dispatches task groups, not mesh groups directly -- one task
    // group per up-to-kMeshletTaskGroupSize candidate meshlets of the chosen LOD.
    uint taskGroupCount = (lodInfo.uMeshletCount + kMeshletTaskGroupSize - 1) / kMeshletTaskGroupSize;

    // Cull mode is fixed-function pipeline state, so double-sided materials cannot share a
    // backface-culling pipeline with single-sided ones. The bundle is split into eight regions
    // (SKILL.md gotcha 4): region = uPass * 4 + alphaTested * 2 + doubleSided, each replayed by
    // its own execute call with its own count slot. The alpha-tested split is pure ordering: the
    // host replays regions in order, so every alpha-tested draw (whose pixel shader branches on
    // the material flag and clips) lands after every fully opaque one.
    GPUMaterial material = AGFXStructuredBuffer<GPUMaterial>::Create(g_Constants.rMaterialBuffer).Load(instance.uMaterialSlot);
    bool doubleSided = GPUMaterialIsDoubleSided(material);
    bool alphaTested = GPUMaterialIsAlphaTested(material);
    uint region = g_Constants.uPass * 4 + (alphaTested ? 2 : 0) + (doubleSided ? 1 : 0);
    uint commandOffset = region * g_Constants.uRegionCapacity;

    uint drawWord = SceneMakeDrawWord(index, lod, fade, outgoing);

    AGFXIndirectDrawMeshBundle bundle = AGFXIndirectDrawMeshBundle::Create(g_Constants.uBundleHandle);
    uint slot = bundle.DrawMesh(commandOffset, region, drawWord, taskGroupCount, 1, 1);

#if defined(AGFX_VULKAN)
    // Vulkan's AGFX_DRAW_ID() in the consuming mesh shader is the linear slot, not the drawId
    // written above -- record slot -> drawWord so the consumer can recover it (SKILL.md gotcha 6).
    // The slot is region-relative, so offset by the region start; the consumer gets the same base
    // through its per-region push constants (uDrawIndirectionBase).
    AGFXRWByteAddressBuffer indirection = AGFXRWByteAddressBuffer::Create(g_Constants.rDrawIndirection);
    indirection.Store((commandOffset + slot) * 4, drawWord);
#endif
}

[numthreads(64, 1, 1)]
void PopulateOpaqueIndirectBundleCS(uint3 dtid : SV_DispatchThreadID) {
    uint index = dtid.x;
    if (index >= g_Constants.uInstanceCount)
        return;

    GPUInstance instance = AGFXStructuredBuffer<GPUInstance>::Create(g_Constants.rInstanceBuffer).Load(index);
    FrameConstants frame = AGFXStructuredBuffer<FrameConstants>::Create(g_Constants.rFrameConstants).Load(0);

    bool cullEnabled = (g_Constants.uCullFlags & kCullFlagEnabled) != 0;

    // Everything persisted across frames hangs off the state slot, never off `index` -- see the
    // comment on SceneStateSlot in GPUScene.hlsli. A freshly handed-out slot still holds whatever
    // the instance that previously owned it left behind, so it counts as invalid for one frame.
    uint slot = SceneStateSlot(instance);
    bool flagsValid = (g_Constants.uCullFlags & kCullFlagVisibilityValid) != 0
                   && !SceneStateIsFresh(instance);

    float3 worldMin, worldMax;
    ComputeWorldBounds(instance, worldMin, worldMax);
    bool frustumVisible = !cullEnabled || IsInstanceVisible(worldMin, worldMax, frame);

    // The early pass advances the LOD state machine for every instance, visible or not; the late
    // pass reads what it wrote, so both passes agree on committed/outgoing/fade within the frame.
    uint lodState = g_Constants.uPass == kPopulatePassEarly
                  ? AdvanceLodState(slot, instance, frame, worldMin, worldMax, flagsValid)
                  : AGFXRWStructuredBuffer<uint>::Create(g_Constants.rLodStateBuffer).Load(slot);
    uint committedLod = (lodState & kLodStateCommittedMask) - 1;
    uint outgoingBiased = (lodState >> kLodStateOutgoingShift) & 0xFu;
    uint fade = LodStateFadeNibble(lodState);

    AGFXRWStructuredBuffer<uint> visibility = AGFXRWStructuredBuffer<uint>::Create(g_Constants.rVisibilityBuffer);

    if (g_Constants.uPass == kPopulatePassEarly) {
        // No occlusion test here: the flag left behind by last frame's late pass already encodes
        // one, and there is no fresh pyramid to test against until this pass's depth exists. The LOD
        // field is preserved (the meshlet bits still describe that LOD) unless the flags are not
        // trustworthy, in which case the whole word resets to a known zero.
        //
        // Both halves of a cross-fade are drawn here, not just the committed LOD. The two passes only
        // work because the early one lays down the full depth of everything that was visible last
        // frame -- the pyramid built from it is what the late pass and every occlusion test rely on.
        // A dithered instance whose other half is missing punches a hole in that depth exactly the
        // size of the object, so the pyramid stops occluding anything behind it and the instance's
        // own late-pass occlusion test has nothing to stand on. The two halves cover complementary
        // Bayer thresholds, so together they still write each pixel exactly once.
        //
        // This stays out of the meshlet visibility bookkeeping: SceneAS skips all of it for outgoing
        // draws in both passes, so the bits still describe exactly one LOD per instance.
        uint old = visibility.Load(slot);
        bool wasVisible = flagsValid && (old & kInstanceVisVisible) != 0;
        bool drawnEarly = wasVisible && frustumVisible;

        uint next = 0;
        if (drawnEarly) {
            EmitDraw(index, instance, committedLod, fade, false);
            if (outgoingBiased != 0)
                EmitDraw(index, instance, min(outgoingBiased - 1, instance.uLod), fade, true);
            bool bitsMatchLod = (old & kInstanceVisLodMask) == ((committedLod + 1) << kInstanceVisLodShift);
            next = (old & kInstanceVisLodMask) | kInstanceVisVisible | kInstanceVisDrawnEarly
                 | (bitsMatchLod ? kInstanceVisBitsLodValid : 0u);
        } else if (flagsValid) {
            next = old & kInstanceVisLodMask;
        }
        visibility.Store(slot, next);
        return;
    }

    uint old = visibility.Load(slot);
    bool drawnEarly = (old & kInstanceVisDrawnEarly) != 0;

    // The occlusion test projects with the pyramid's own camera (mHZBViewProjection), never the
    // frozen frustum. scene.freeze_frustum still freezes the frustum planes and the LOD camera
    // position, which is what it is for.
    bool occluded = false;
    if (cullEnabled && (g_Constants.uCullFlags & kCullFlagHZBValid) != 0 && frustumVisible) {
        HZBParams hzb = HZBMakeParams(g_Constants.rHZB, g_Constants.uHZBWidth, g_Constants.uHZBHeight, g_Constants.uHZBMipCount);
        occluded = HZBIsOccluded(worldMin, worldMax, frame.mHZBViewProjection, hzb);
    }

    if (!frustumVisible || occluded) {
        visibility.Store(slot, old & kInstanceVisLodMask);
        return;
    }

    if (cullEnabled) {
        // Emit every visible instance, drawn early or not: SceneAS re-tests all its meshlets against
        // the fresh pyramid, draws only the ones the early pass skipped, and rewrites the meshlet
        // visibility bits for the LOD recorded here. The outgoing half of a cross-fade is a second,
        // short-lived draw that stays out of the meshlet-bits bookkeeping entirely (see SceneAS) --
        // which is exactly why it needs the !drawnEarly guard the incoming half does not: with no
        // per-meshlet record of what the early pass drew, re-emitting it here would draw it twice.
        EmitDraw(index, instance, committedLod, fade, false);
        if (outgoingBiased != 0 && !drawnEarly)
            EmitDraw(index, instance, min(outgoingBiased - 1, instance.uLod), fade, true);
        bool bitsMatchLod = (old & kInstanceVisLodMask) == ((committedLod + 1) << kInstanceVisLodShift);
        visibility.Store(slot, ((committedLod + 1) << kInstanceVisLodShift) | kInstanceVisVisible
                              | (drawnEarly ? kInstanceVisDrawnEarly : 0u)
                              | (bitsMatchLod ? kInstanceVisBitsLodValid : 0u));
        return;
    }

    // Culling disabled: the early pass already drew everything visible last frame in full, so only
    // emit the difference, and mark the meshlet bits stale so re-enabling culling starts conservative.
    visibility.Store(slot, kInstanceVisVisible | (drawnEarly ? kInstanceVisDrawnEarly : 0u));
    if (!drawnEarly) {
        EmitDraw(index, instance, committedLod, fade, false);
        if (outgoingBiased != 0)
            EmitDraw(index, instance, min(outgoingBiased - 1, instance.uLod), fade, true);
    }
}
