/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 10:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Asset/Model.hpp>
#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/MaterialScheme.hpp>
#include <Caramel/Renderer/StreamBuffer.hpp>
#include <Caramel/Scene/RenderInstance.hpp>

#include <AGFX/agfx.hpp>
#include <glm/glm.hpp>

class StreamingManager;
struct MaterialOverride;

/// @brief Mirrors `GPUMaterial` in Content/Shaders/Common/GPUScene.hlsli field-for-field. 80 bytes.
struct GPUMaterial
{
    glm::vec4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };    // 0
    glm::vec4 emissiveFactor{ 0.0f, 0.0f, 0.0f, 0.0f };     // 16  w unused
    float32   metallicFactor = 1.0f;                        // 32
    float32   roughnessFactor = 1.0f;                       // 36
    float32   alphaCutoff = 0.5f;                           // 40
    uint32    flags = 0;                                    // 44  bit0 = doubleSided, bit1 = metallic
                                                              //     overridden (ignore texture), bit2 =
                                                              //     roughness overridden (ignore texture),
                                                              //     bit3 = alphaTested (alphaMode "Mask"/
                                                              //     "Blend" until a translucent pass exists)
    uint32    textures[5]{};                                // 48  indexed by MaterialTextureSlot
    uint32    schemeId = 0;                                 // 68  for debugging/validation only
    uint32    _pad[2]{};                                    // 72
};
static_assert(sizeof(GPUMaterial) == 80, "GPUMaterial must stay 16-byte aligned and match GPUScene.hlsli");

/// @brief Mirrors `GPUInstance` in Content/Shaders/Common/GPUScene.hlsli field-for-field. 208 bytes.
///
/// Bounds are vec4 rather than vec3 on purpose: a glm::vec3 in an HLSL structured buffer is a
/// classic alignment trap.
struct GPUInstance
{
    glm::mat4 transform{ 1.0f };                            // 0
    glm::vec4 boundsMin{ 0.0f };                            // 64  w unused, mesh-level
    glm::vec4 boundsMax{ 0.0f };                            // 80
    uint32    materialSlot = 0;                             // 96
    uint32    vertexBuffer = 0;                             // 100  handles for the *resident* LOD
    uint32    meshletBuffer = 0;                            // 104
    uint32    meshletVertexBuffer = 0;                      // 108
    uint32    meshletTriangleBuffer = 0;                    // 112
    uint32    meshletCount = 0;                             // 116
    uint32    lod = 0;                                      // 120
    uint32    meshletBoundsBuffer = 0;                      // 124  handle for this instance's *resident* LOD
    glm::mat4 prevTransform{ 1.0f };                        // 128  last frame's transform, for motion vectors

    // Index into the renderer's persistent per-instance state (LOD cross-fade state, instance and
    // meshlet visibility). NOT the instance index: Build compacts by residency, so an instance's
    // position in the instance buffer changes whenever any earlier model streams in or out, and
    // state indexed that way silently migrates to a different object. Allocated from a free list
    // keyed by the same stable entity identity as prevTransform, so it survives compaction.
    uint32    stateSlot = 0;                                // 192
    uint32    stateFresh = 0;                               // 196  1 = slot just allocated, whatever
                                                            //      it holds belonged to someone else
    glm::uvec2 statePad{ 0u };                              // 200
};
static_assert(sizeof(GPUInstance) == 208, "GPUInstance must stay 16-byte aligned and match GPUScene.hlsli");

/// @brief Mirrors `GPULodInfo` in Content/Shaders/Common/GPUScene.hlsli field-for-field. 20 bytes,
/// tight-packed (no forced 16-byte alignment needed -- structured buffers in this codebase's HLSL
/// use natural packing, unlike AGFX_PUSH_CONSTANTS/cbuffer types). One entry per (instance, lod)
/// pair, indexed instanceIndex * CaramelAsset::kLodCount + lod.
struct GPULodInfo
{
    uint32 meshletBuffer = 0;
    uint32 meshletVertexBuffer = 0;
    uint32 meshletTriangleBuffer = 0;
    uint32 meshletBoundsBuffer = 0;
    uint32 meshletCount = 0;
};
static_assert(sizeof(GPULodInfo) == 20, "GPULodInfo must match GPUScene.hlsli's GPULodInfo");

/// @brief One CPU-side draw, parallel to the compacted instance buffer. CPU-only, so carrying the
/// sort keys here is free.
struct GPUDraw
{
    uint32 instanceIndex = 0;
    uint32 meshletCount = 0;
    uint32 schemeId = 0;
    uint32 materialSlot = 0;
};

/// @brief A run of draws in the sorted draw list sharing one material slot.
///
/// This level changes no GPU state today -- all material data is already bindless -- so it is pure
/// bookkeeping. It exists because it is the unit that becomes one agfx::IndirectBundle region once
/// draw submission moves to the GPU (see .claude/agfx-mdi/SKILL.md gotcha 4).
struct MaterialBatch
{
    uint32 materialSlot = 0;
    uint32 firstDraw = 0;
    uint32 drawCount = 0;
};

/// @brief A run of material batches sharing one scheme, and therefore one pipeline. This is the
/// level that is load-bearing: it is exactly where SetPipeline changes.
struct SchemeBucket
{
    uint32 schemeId = 0;
    uint32 firstBatch = 0;
    uint32 batchCount = 0;
};

/// @brief Per-frame GPU-visible view of the scene: one bindless structured buffer of instances and
/// one of materials, both indexed by the shader.
///
/// Both buffers are rebuilt in full every frame. That is deliberate: bindless handles in this engine
/// are volatile -- StreamingTexture reassigns its handle on every mip promotion and StreamingModel's
/// meshlet handles change on every LOD promotion, neither with any notification. Rebuilding is
/// immune to that staleness by construction and costs the same as the full memcpy the renderer was
/// already doing. The shader-facing layout is identical to what sparse dirty-patching would need, so
/// switching later requires no shader change.
class GPUScene
{
public:
    void Init(agfx::Device& device, const SchemeRegistry& schemes, uint32 framesInFlight);

    /// @brief Rebuilds the buffers for `frameIndex` and re-sorts draws into scheme/material buckets.
    /// Instances whose LOD is not yet resident are skipped, so instance indices are compacted.
    void Build(StreamingManager& streamingManager, const TArray<RenderInstance>& renderInstances, uint32 frameIndex);

    /// @brief Draws sorted by (schemeId, materialSlot). Iterate via GetBuckets(), not directly.
    const TArray<GPUDraw>& GetDraws() const { return m_Draws; }
    const TArray<MaterialBatch>& GetBatches() const { return m_Batches; }
    const TArray<SchemeBucket>& GetBuckets() const { return m_Buckets; }

    agfx::BufferView& GetInstanceBufferView(uint32 frameIndex) { return m_InstanceStream.views[frameIndex]; }
    agfx::BufferView& GetMaterialBufferView(uint32 frameIndex) { return m_MaterialStream.views[frameIndex]; }

    /// @brief Per-instance, per-LOD meshlet buffer handles + counts for every LOD (0..kLodCount-1),
    /// not just the resident one baked into GetInstanceBufferView()'s GPUInstance entries -- lets
    /// PopulateOpaqueIndirectBundleCS pick a LOD by camera distance instead of always the resident one.
    agfx::BufferView& GetInstanceLodTableView(uint32 frameIndex) { return m_InstanceLodStream.views[frameIndex]; }

    /// @brief Parameter buffer for one scheme, indexed by global material slot. Null view when the
    /// scheme declares no parameters -- its shader never reads it.
    agfx::BufferView& GetSchemeParamBufferView(uint32 schemeId, uint32 frameIndex) { return m_SchemeParamStreams[schemeId].views[frameIndex]; }

    uint32 GetMaterialSlotCount() const { return (uint32)m_MaterialStaging.Size(); }

    /// @brief Number of resident, non-culled instances in this frame's compacted instance buffer --
    /// the exact count the indirect-bundle populate shader should dispatch over.
    uint32 GetInstanceCount() const { return (uint32)m_InstanceStaging.Size(); }

    uint64 GetPreCullMeshletCount() const { return m_PreCullMeshlets; }
    uint64 GetPreCullTriangleCount() const { return m_PreCullTriangles; }

    /// @brief Largest meshlet count any (instance, LOD) pair in this frame's build can dispatch --
    /// sizes the per-instance stride of the renderer's meshlet visibility bitfield.
    uint32 GetMaxMeshletCount() const { return m_MaxMeshletCount; }

    /// @brief High-water mark of allocated state slots -- how many entries the renderer's persistent
    /// per-instance buffers need. Always >= GetInstanceCount(), since freed slots stay reserved
    /// until they are recycled.
    uint32 GetStateSlotCount() const { return m_StateSlotCount; }

    /// @brief CPU-side view of this frame's compacted instance buffer (transform + local bounds),
    /// e.g. for debug-drawing instance AABBs.
    const TArray<GPUInstance>& GetInstances() const { return m_InstanceStaging; }

    /// @brief The handle a material's texture slot holds when no texture is bound (or it has no
    /// resident mip yet). Shaders that must treat "no texture" differently from "sampled zero"
    /// (e.g. normal mapping, where a missing map should fall back to the geometric normal instead
    /// of decoding whatever undefined pixel the fallback texture holds) compare against this.
    uint32 GetFallbackTextureHandle() const { return (uint32)m_FallbackTextureView.GetHandle(); }

private:
    /// @brief Returns the slot for this instance's material, allocating one on first sight. An
    /// instance whose entity overrides this material resolves to a separate derived slot, and
    /// `outOverride` receives that override.
    ///
    /// Serial only -- it bumps the slot counter and writes the override's cached slot. Build's
    /// worker threads call LookupMaterialSlot instead and defer the misses to the serial phase.
    ///
    /// `outOverride` is only valid for the duration of the current Build() -- it points into the
    /// entity's `materialOverrides` array, which reallocates whenever a new override is added, so it
    /// must never be stored across frames.
    uint32 ResolveMaterialSlot(const RenderInstance& instance, const MaterialOverride*& outOverride);

    /// @brief Read-only half of ResolveMaterialSlot, safe to call from several threads at once.
    /// Returns UINT32_MAX when this instance has no slot allocated yet.
    uint32 LookupMaterialSlot(const RenderInstance& instance) const;

    /// @brief The material override this instance resolves to, or null. Mirrors
    /// Scene::FindMaterialOverride: an override pinned to this exact mesh slot wins over the
    /// entity-wide one, so a per-mesh edit does not spill onto sibling meshes that merely happen to
    /// share a material index.
    MaterialOverride* FindActiveOverride(const RenderInstance& instance) const;

    void WriteMaterial(uint32 slot, const ModelMaterial& source, const MaterialOverride* activeOverride,
                       StreamingManager& streamingManager, uint32 requestId, int32 materialIndex);

    uint32 ResolveTextureHandle(StreamingManager& streamingManager, uint32 requestId, int32 materialIndex,
                                MaterialTextureSlot slot) const;

    /// @brief Orders the used material slots into batches and scheme buckets, and seeds
    /// m_SlotDrawCursor with each slot's base offset in the draw list.
    ///
    /// This replaces sorting the draw list itself: the sort key is (schemeId, materialSlot) and
    /// schemeId is a function of materialSlot, so ordering the handful of *used slots* and then
    /// scattering draws into their slot's range is the same result in O(draws) instead of
    /// O(draws log draws).
    void BuildBuckets();

    // One resident instance, everything the parallel fill phase needs without touching shared
    // mutable state. Deliberately small: Build keeps one entry per *render instance* (not per
    // surviving instance) so the phases can run over a fixed index space, and the serial phase
    // streams over the flat side arrays below rather than over this.
    struct BuildItem
    {
        const RenderInstance* instance = nullptr;
        uint64 identity = 0;
        uint32 lod = 0;
        uint32 meshletCount = 0;
    };

    // First instance seen using a slot this frame -- WriteMaterial runs once per slot, not once per
    // instance (texture handles are volatile, so it still runs every frame).
    struct MaterialWrite
    {
        uint32 slot = 0;
        StreamingModel* model = nullptr;
        const MaterialOverride* activeOverride = nullptr;
    };

    // Per-worker accumulators for the reductions the gather phase performs, one cache line each so
    // workers never share one.
    struct alignas(64) BuildScratch
    {
        uint64 preCullMeshlets = 0;
        uint64 preCullTriangles = 0;
        uint32 maxMeshletCount = 0;
    };

    enum : uint8
    {
        kItemValid = 1,       // survived residency filtering, has an entry in every side array
        kItemStateFresh = 2,  // its state entry was created this frame, so its state slot holds junk
    };

    TArray<BuildItem> m_BuildItems;
    TArray<uint8> m_ItemFlags;
    TArray<uint32> m_ItemSlot;      // material slot, UINT32_MAX until the serial phase allocates one
    TArray<uint32> m_ItemState;     // index into m_States, kNoState until the serial phase creates it
    TArray<uint32> m_ItemDst;       // index in the compacted instance buffer
    TArray<uint32> m_ItemDrawDst;   // index in the slot-ordered draw list
    TArray<BuildScratch> m_Scratch;

    TArray<MaterialWrite> m_MaterialWrites;
    TArray<uint8> m_SlotWrittenThisBuild;
    TArray<uint32> m_SlotDrawCount;
    TArray<uint32> m_SlotDrawCursor;
    TArray<uint32> m_OrderedSlots;

    void UploadSchemeParams(uint32 frameIndex);

    agfx::Device* m_Device = nullptr;
    const SchemeRegistry* m_Schemes = nullptr;

    // Sampled in place of any texture that has no resident mip yet (or no path at all).
    agfx::Texture m_FallbackTexture;
    agfx::TextureView m_FallbackTextureView;

    StreamBuffer m_InstanceStream;
    StreamBuffer m_InstanceLodStream;
    StreamBuffer m_MaterialStream;

    TArray<GPUInstance> m_InstanceStaging;
    TArray<GPULodInfo> m_InstanceLodStaging; // instanceStagingIndex * CaramelAsset::kLodCount + lod
    TArray<GPUMaterial> m_MaterialStaging;
    TArray<GPUDraw> m_Draws;
    uint64 m_PreCullMeshlets = 0;
    uint64 m_PreCullTriangles = 0;
    uint32 m_MaxMeshletCount = 0;
    TArray<MaterialBatch> m_Batches;
    TArray<SchemeBucket> m_Buckets;

    // One parameter buffer per scheme, each indexed by *global* material slot. That is sparse -- a
    // scheme's buffer has room for slots belonging to other schemes -- but it removes an entire
    // slot-remapping layer, and at a handful of schemes it costs tens of KB.
    TArray<StreamBuffer> m_SchemeParamStreams;
    TArray<TArray<uint8>> m_SchemeParamStaging;

    // Everything about a render instance that has to survive across frames, keyed by stable entity
    // identity rather than instance index (Build compacts by residency, so indices change meaning
    // across frames). Key: (owner->id << 40) | (meshSlot << 24) | instanceIndex, mesh pointer when
    // ownerless.
    //
    // - prevTransform feeds motion vectors.
    // - stateSlot indexes the renderer's persistent per-instance buffers, which outlive a single
    //   Build and so cannot be indexed by the compacted instance index. Slots belonging to
    //   instances that stopped being submitted are recycled, and a recycled slot is handed out
    //   marked fresh so its next owner never inherits stale LOD/visibility state.
    struct InstanceState
    {
        uint64 identity = 0;
        glm::mat4 prevTransform{ 1.0f };
        uint32 stateSlot = 0;
    };

    static constexpr uint32 kNoState = UINT32_MAX;

    /// @brief Index into m_States for this identity, or kNoState. Read-only, so Build's gather phase
    /// calls it from every worker at once.
    uint32 FindState(uint64 identity) const;

    /// @brief Allocates a state entry (and a state slot) for an identity seen for the first time.
    uint32 CreateState(uint64 identity, const glm::mat4& transform);

    void InsertStateIndex(uint64 identity, uint32 entry);
    void RehashStates();

    /// @brief Frees the state slots of identities that were not submitted this frame and compacts
    /// m_States. Must run after the fill phase -- compaction moves entries, invalidating m_ItemState.
    void SweepDeadStates();

    // Dense entry array plus a separate open-addressed index. This used to be three TDictionary
    // (i.e. std::unordered_map) instances, two of which were cleared and refilled every frame:
    // ~300k node allocations per frame at 100k instances, which is where most of Build's CPU time
    // went. Nothing here allocates in steady state.
    TArray<InstanceState> m_States;

    // Frame stamp per entry, parallel to m_States and kept out of it so the per-frame liveness scan
    // streams over 4 bytes per entry instead of 84.
    TArray<uint32> m_StateSeen;

    // Power-of-two open-addressed table of indices into m_States, kNoState marking an empty bucket.
    TArray<uint32> m_StateIndex;

    TArray<uint32> m_FreeStateSlots;
    uint32 m_StateSlotCount = 0;
    uint32 m_BuildCounter = 0;
    bool m_WarnedStateSlotCollision = false;

    // Slots persist across frames so bucketing gets temporal coherence; contents are rewritten each
    // frame. Key: (requestId << 32) | (uint32)materialIndex.
    TDictionary<uint64, uint32> m_BaseSlots;
    uint32 m_NextMaterialSlot = 0;

    // Per material slot, parallel to m_MaterialStaging: which scheme it uses and the packed
    // parameter values to publish for it this frame.
    TArray<uint32> m_SlotSchemeId;
};
