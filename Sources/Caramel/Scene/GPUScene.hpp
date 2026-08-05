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
                                                              //     roughness overridden (ignore texture)
    uint32    textures[5]{};                                // 48  indexed by MaterialTextureSlot
    uint32    schemeId = 0;                                 // 68  for debugging/validation only
    uint32    _pad[2]{};                                    // 72
};
static_assert(sizeof(GPUMaterial) == 80, "GPUMaterial must stay 16-byte aligned and match GPUScene.hlsli");

/// @brief Mirrors `GPUInstance` in Content/Shaders/Common/GPUScene.hlsli field-for-field. 128 bytes.
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
};
static_assert(sizeof(GPUInstance) == 128, "GPUInstance must stay 16-byte aligned and match GPUScene.hlsli");

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
    /// `outOverride` is only valid for the duration of the current Build() -- it points into the
    /// entity's `materialOverrides` array, which reallocates whenever a new override is added, so it
    /// must never be stored across frames.
    uint32 ResolveMaterialSlot(const RenderInstance& instance, const MaterialOverride*& outOverride);

    void WriteMaterial(uint32 slot, const ModelMaterial& source, const MaterialOverride* activeOverride,
                       StreamingManager& streamingManager, uint32 requestId, int32 materialIndex);

    uint32 ResolveTextureHandle(StreamingManager& streamingManager, uint32 requestId, int32 materialIndex,
                                MaterialTextureSlot slot) const;

    /// @brief Groups the sorted draw list into material batches and scheme buckets.
    void BuildBuckets();

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
    TArray<MaterialBatch> m_Batches;
    TArray<SchemeBucket> m_Buckets;

    // One parameter buffer per scheme, each indexed by *global* material slot. That is sparse -- a
    // scheme's buffer has room for slots belonging to other schemes -- but it removes an entire
    // slot-remapping layer, and at a handful of schemes it costs tens of KB.
    TArray<StreamBuffer> m_SchemeParamStreams;
    TArray<TArray<uint8>> m_SchemeParamStaging;

    // Slots persist across frames so bucketing gets temporal coherence; contents are rewritten each
    // frame. Key: (requestId << 32) | (uint32)materialIndex.
    TDictionary<uint64, uint32> m_BaseSlots;
    uint32 m_NextMaterialSlot = 0;

    // Per material slot, parallel to m_MaterialStaging: which scheme it uses and the packed
    // parameter values to publish for it this frame.
    TArray<uint32> m_SlotSchemeId;
};
