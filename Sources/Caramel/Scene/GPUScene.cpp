/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 10:05:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "GPUScene.hpp"

#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Core/JobSystem.hpp>
#include <Caramel/Core/CpuProfiler.hpp>
#include <Caramel/Core/Logger.hpp>
#include <Caramel/Scene/SceneNode.hpp>

#include <algorithm>

namespace
{
    uint64 BaseSlotKey(uint32 requestId, int32 materialIndex)
    {
        return ((uint64)requestId << 32) | (uint64)(uint32)materialIndex;
    }
}

void GPUScene::Init(agfx::Device& device, const SchemeRegistry& schemes, uint32 framesInFlight)
{
    m_Device = &device;
    m_Schemes = &schemes;

    m_InstanceStream.Init(framesInFlight);
    m_InstanceLodStream.Init(framesInFlight);
    m_MaterialStream.Init(framesInFlight);

    m_SchemeParamStreams.Resize(schemes.Count());
    m_SchemeParamStaging.Resize(schemes.Count());
    for (uint32 i = 0; i < schemes.Count(); ++i)
        m_SchemeParamStreams[i].Init(framesInFlight);

    agfx::TextureCreateInfo textureInfo;
    textureInfo.SetType(agfx::TextureType::Texture2D)
               .SetFormat(agfx::TextureFormat::RGBA8Unorm)
               .SetUsage(agfx::TextureUsage::Sampled)
               .SetSize(1, 1, 1)
               .SetMipLevels(1);
    m_FallbackTexture = m_Device->CreateTexture(textureInfo);
    m_FallbackTexture.SetName("GPUScene Fallback Texture");

    agfx::TextureViewCreateInfo viewInfo;
    viewInfo.SetTexture(m_FallbackTexture.Get())
            .SetFormat(agfx::TextureFormat::RGBA8Unorm)
            .SetType(agfx::TextureType::Texture2D)
            .SetMipRange(0, 1)
            .SetArrayRange(0, 1)
            .SetWriteable(false);
    m_FallbackTextureView = m_Device->CreateTextureView(viewInfo);

    m_Device->MakeResourcesResident();
}

uint32 GPUScene::ResolveMaterialSlot(const RenderInstance& instance, const MaterialOverride*& outOverride)
{
    outOverride = nullptr;

    uint32 requestId = instance.mesh->GetRequestId();
    int32 materialIndex = instance.mesh->GetMesh().materialIndex;

    // An entity that overrides this material gets its own derived slot, so two entities instanced
    // from the same model no longer share (and clobber) each other's factors.
    if (instance.owner)
    {
        // Mirrors Scene::FindMaterialOverride: an override pinned to this exact mesh slot wins over
        // the entity-wide one, so a per-mesh edit does not spill onto sibling meshes that merely
        // happen to share a material index.
        MaterialOverride* chosen = nullptr;
        for (MaterialOverride& matOverride : instance.owner->materialOverrides)
        {
            if (matOverride.materialIndex != materialIndex || !matOverride.HasAnyOverride())
                continue;

            if (matOverride.meshSlot == (int32)instance.meshSlot)
            {
                chosen = &matOverride;
                break;
            }

            if (matOverride.meshSlot == MaterialOverride::kAllMeshes)
                chosen = &matOverride;
        }

        if (chosen)
        {
            if (chosen->gpuMaterialSlot == UINT32_MAX)
                chosen->gpuMaterialSlot = m_NextMaterialSlot++;

            outOverride = chosen;
            return chosen->gpuMaterialSlot;
        }
    }

    uint64 key = BaseSlotKey(requestId, materialIndex);
    auto it = m_BaseSlots.Find(key);
    if (it != m_BaseSlots.End())
        return it->second;

    uint32 slot = m_NextMaterialSlot++;
    m_BaseSlots[key] = slot;
    return slot;
}

uint32 GPUScene::ResolveTextureHandle(StreamingManager& streamingManager, uint32 requestId, int32 materialIndex,
                                      MaterialTextureSlot slot) const
{
    TShared<StreamingTexture> texture = streamingManager.GetMaterialTexture(requestId, materialIndex, slot);
    if (texture && texture->SnapshotResidentMip() != StreamingTexture::kNoResidentMip)
        return (uint32)(intptr_t)texture->GetDisplayTexID();

    return (uint32)m_FallbackTextureView.GetHandle();
}

void GPUScene::WriteMaterial(uint32 slot, const ModelMaterial& source, const MaterialOverride* activeOverride,
                             StreamingManager& streamingManager, uint32 requestId, int32 materialIndex)
{
    GPUMaterial& dst = m_MaterialStaging[slot];

    // `source` is the pristine cooked material -- nothing mutates CPUModel any more, so overrides are
    // layered on here at write time and un-ticking one genuinely restores the cooked value.
    dst.baseColorFactor = source.baseColorFactor;
    dst.emissiveFactor = glm::vec4(source.emissiveFactor, 0.0f);
    dst.metallicFactor = source.metallicFactor;
    dst.roughnessFactor = source.roughnessFactor;

    uint32 flags = source.doubleSided ? 1u : 0u;
    if (source.alphaMode == "Mask" || source.alphaMode == "Blend")
        flags |= 8u;

    if (activeOverride)
    {
        if (activeOverride->overrideBaseColor)
            dst.baseColorFactor = activeOverride->baseColorFactor;
        if (activeOverride->overrideMetallic)
        {
            dst.metallicFactor = activeOverride->metallicFactor;
            flags |= 2u; // shader must use the factor as-is, not multiply it into the texture channel
        }
        if (activeOverride->overrideRoughness)
        {
            dst.roughnessFactor = activeOverride->roughnessFactor;
            flags |= 4u;
        }
        if (activeOverride->overrideEmissive)
            dst.emissiveFactor = glm::vec4(activeOverride->emissiveFactor, 0.0f);
    }

    dst.alphaCutoff = source.alphaCutoff;
    dst.flags = flags;

    uint32 schemeId = m_Schemes->FindId(activeOverride ? activeOverride->schemeName : String());
    dst.schemeId = schemeId;
    m_SlotSchemeId[slot] = schemeId;

    // Publish this slot's parameter block into its scheme's buffer. Indexed by slot, so the write
    // position is simply slot * stride.
    const MaterialScheme& scheme = m_Schemes->Get(schemeId);
    if (scheme.paramStride > 0)
    {
        TArray<uint8>& staging = m_SchemeParamStaging[schemeId];
        uint64 required = (uint64)(slot + 1) * scheme.paramStride;
        if (staging.Size() < required)
            staging.Resize(required, 0);

        static const TDictionary<String, glm::vec4> kNoValues;
        scheme.PackParams(activeOverride ? activeOverride->schemeParamValues : kNoValues,
                          staging.Data() + (uint64)slot * scheme.paramStride);
    }

    for (uint32 i = 0; i < (uint32)MaterialTextureSlot::Count; ++i)
        dst.textures[i] = ResolveTextureHandle(streamingManager, requestId, materialIndex, (MaterialTextureSlot)i);
}

void GPUScene::Build(StreamingManager& streamingManager, const TArray<RenderInstance>& renderInstances, uint32 frameIndex)
{
    CARAMEL_ZONE("GPUScene::Build");

    m_Draws.Clear();
    m_PreCullMeshlets = 0;
    m_PreCullTriangles = 0;
    m_MaxMeshletCount = 0;
    m_BuildItems.Clear();
    m_MaterialWrites.Clear();
    m_SlotWrittenThisBuild.Clear();

    // Serial phase: residency filtering and slot resolution mutate shared state (slot maps, staging
    // sizes) and are cheap; everything per-instance and per-material moves to the phases below.
    {
        CARAMEL_ZONE("Resolve Slots (serial)");
        for (const RenderInstance& instance : renderInstances)
        {
            StreamingModel& model = *instance.mesh;

            uint32 lod = model.SnapshotResidentLOD();
            if (lod == StreamingModel::kNoResidentLOD)
                continue;

            uint32 meshletCount = model.GetMeshletCount(lod);
            if (meshletCount == 0)
                continue;

            const MaterialOverride* activeOverride = nullptr;
            uint32 materialSlot = ResolveMaterialSlot(instance, activeOverride);
            if (materialSlot >= m_MaterialStaging.Size())
            {
                m_MaterialStaging.Resize(materialSlot + 1);
                m_SlotSchemeId.Resize(materialSlot + 1, SchemeRegistry::kDefaultSchemeId);
            }

            if (materialSlot >= m_SlotWrittenThisBuild.Size())
                m_SlotWrittenThisBuild.Resize(materialSlot + 1, 0);
            if (!m_SlotWrittenThisBuild[materialSlot])
            {
                m_SlotWrittenThisBuild[materialSlot] = 1;
                m_MaterialWrites.PushBack(MaterialWrite{ materialSlot, &model, activeOverride });
            }

            for (uint32 lodIdx = 0; lodIdx < CaramelAsset::kLodCount; ++lodIdx)
                m_MaxMeshletCount = std::max(m_MaxMeshletCount, model.GetMeshletCount(lodIdx));

            m_PreCullMeshlets += meshletCount;
            m_PreCullTriangles += model.GetMesh().lods[lod].GetFlatIndexCount() / 3;

            // Field widths matter: this keys the state slot, so two instances colliding here share
            // one slot and therefore one meshlet-visibility bit range, which ORs their visible sets
            // together and quietly disables meshlet culling for both. The old 12-bit instanceIndex
            // field overflowed into meshSlot past 4096 instances under one node.
            uint64 identity = instance.owner
                ? (((uint64)instance.owner->id << 40) | ((uint64)instance.meshSlot << 24) | instance.instanceIndex)
                : ((uint64)(uintptr_t)instance.mesh << 24) | instance.instanceIndex;
            auto prevIt = m_PrevTransforms.Find(identity);
            glm::mat4 prevTransform = prevIt != m_PrevTransforms.End() ? prevIt->second : instance.transform;

            // Same identity -> same state slot for as long as the instance keeps being submitted,
            // whatever the compaction does to its instance index in between.
            uint32 stateSlot;
            bool stateFresh;
            auto slotIt = m_StateSlots.Find(identity);
            if (slotIt != m_StateSlots.End())
            {
                stateSlot = slotIt->second;
                stateFresh = false;
            }
            else if (!m_FreeStateSlots.IsEmpty())
            {
                stateSlot = m_FreeStateSlots[m_FreeStateSlots.Size() - 1];
                m_FreeStateSlots.PopBack();
                stateFresh = true;
            }
            else
            {
                stateSlot = m_StateSlotCount++;
                stateFresh = true;
            }
            m_NextStateSlots[identity] = stateSlot;

            m_BuildItems.PushBack(BuildItem{ &instance, lod, meshletCount, materialSlot, prevTransform, identity, stateSlot, stateFresh });
        }
    }

    {
        // Rebuilt from scratch so entries for instances that vanished this frame are evicted; a
        // reappearing instance then gets prev = current, i.e. zero object motion on first sight.
        CARAMEL_ZONE("Store Prev Transforms");
        m_PrevTransforms.Clear();
        for (const BuildItem& item : m_BuildItems)
            m_PrevTransforms[item.identity] = item.instance->transform;
    }

    {
        // Recycle the slots of instances that stopped being submitted (streamed out, despawned).
        // They are handed back out marked fresh, so the next owner never inherits their state.
        CARAMEL_ZONE("Recycle State Slots");

        // Fewer slots than instances means two instances hashed to the same identity and are now
        // sharing persistent state -- silent apart from culling quietly degrading, so say so.
        if (m_NextStateSlots.Size() < m_BuildItems.Size() && !m_WarnedStateSlotCollision)
        {
            m_WarnedStateSlotCollision = true;
            CARAMEL_WARN("GPUScene: {} instances share only {} state slots -- identity collision, "
                         "per-instance LOD and visibility state is being shared",
                         m_BuildItems.Size(), m_NextStateSlots.Size());
        }

        for (auto it = m_StateSlots.Begin(); it != m_StateSlots.End(); ++it)
        {
            if (!m_NextStateSlots.Contains(it->first))
                m_FreeStateSlots.PushBack(it->second);
        }
        m_StateSlots = std::move(m_NextStateSlots);
        m_NextStateSlots.Clear();
    }

    {
        // One WriteMaterial per slot per frame instead of one per instance -- same freshness for the
        // volatile texture handles, a fraction of the scheme-param packing and streaming lookups.
        CARAMEL_ZONE("Write Materials");
        for (const MaterialWrite& write : m_MaterialWrites)
            WriteMaterial(write.slot, write.model->GetMaterial(), write.activeOverride, streamingManager,
                          write.model->GetRequestId(), write.model->GetMesh().materialIndex);
    }

    uint32 itemCount = (uint32)m_BuildItems.Size();
    m_InstanceStaging.Resize(itemCount);
    m_InstanceLodStaging.Resize((uint64)itemCount * CaramelAsset::kLodCount);
    m_Draws.Resize(itemCount);

    // Parallel fill: every write below lands at an index owned by exactly one item, and everything
    // read is either per-item or immutable for the rest of this Build.
    {
        CARAMEL_ZONE("Parallel Fill (dispatch)");
        JobSystem::Get().ParallelFor(itemCount, 64, [&](uint32 start, uint32 end, uint32) {
            CARAMEL_ZONE("Parallel Fill (chunk)");
            for (uint32 i = start; i < end; ++i)
            {
                const BuildItem& item = m_BuildItems[i];
                StreamingModel& model = *item.instance->mesh;
                const ModelMesh& mesh = model.GetMesh();

                GPUInstance gpu;
                gpu.transform = item.instance->transform;
                gpu.boundsMin = glm::vec4(mesh.boundsMin, 0.0f);
                gpu.boundsMax = glm::vec4(mesh.boundsMax, 0.0f);
                gpu.materialSlot = item.materialSlot;
                gpu.vertexBuffer = (uint32)model.GetVertexBufferView().GetHandle();
                gpu.meshletBuffer = (uint32)model.GetMeshletBufferView(item.lod).GetHandle();
                gpu.meshletVertexBuffer = (uint32)model.GetMeshletVertexBufferView(item.lod).GetHandle();
                gpu.meshletTriangleBuffer = (uint32)model.GetMeshletTriangleBufferView(item.lod).GetHandle();
                gpu.meshletCount = item.meshletCount;
                gpu.lod = item.lod;
                gpu.meshletBoundsBuffer = (uint32)model.GetMeshletBoundsBufferView(item.lod).GetHandle();
                gpu.prevTransform = item.prevTransform;
                gpu.stateSlot = item.stateSlot;
                gpu.stateFresh = item.stateFresh ? 1u : 0u;
                m_InstanceStaging[i] = gpu;

                // Every LOD's handles + CPU-known meshlet count, not just the resident one baked into
                // `gpu` above -- PopulateOpaqueIndirectBundleCS picks the actual LOD per instance per
                // frame from this. Entry i's LODs occupy [i * kLodCount, (i + 1) * kLodCount).
                for (uint32 lodIdx = 0; lodIdx < CaramelAsset::kLodCount; ++lodIdx)
                {
                    GPULodInfo lodInfo;
                    lodInfo.meshletBuffer = (uint32)model.GetMeshletBufferView(lodIdx).GetHandle();
                    lodInfo.meshletVertexBuffer = (uint32)model.GetMeshletVertexBufferView(lodIdx).GetHandle();
                    lodInfo.meshletTriangleBuffer = (uint32)model.GetMeshletTriangleBufferView(lodIdx).GetHandle();
                    lodInfo.meshletBoundsBuffer = (uint32)model.GetMeshletBoundsBufferView(lodIdx).GetHandle();
                    lodInfo.meshletCount = model.GetMeshletCount(lodIdx);
                    m_InstanceLodStaging[(uint64)i * CaramelAsset::kLodCount + lodIdx] = lodInfo;
                }

                m_Draws[i] = GPUDraw{ i, item.meshletCount, m_SlotSchemeId[item.materialSlot], item.materialSlot };
            }
        });
    }

    {
        // Sort draws into (scheme, material) order. The instance buffer is deliberately NOT reordered
        // -- draws carry an index into it, so this stays a CPU-side reshuffle.
        CARAMEL_ZONE("Sort Draws");
        std::sort(m_Draws.Begin(), m_Draws.End(), [](const GPUDraw& a, const GPUDraw& b) {
            if (a.schemeId != b.schemeId)
                return a.schemeId < b.schemeId;
            return a.materialSlot < b.materialSlot;
        });
    }

    {
        CARAMEL_ZONE("Build Buckets");
        BuildBuckets();
    }

    {
        CARAMEL_ZONE("Upload Streams");
        m_InstanceStream.Upload(*m_Device, frameIndex, "GPUScene Instance Buffer", sizeof(GPUInstance),
                                m_InstanceStaging.Data(), m_InstanceStaging.Size() * sizeof(GPUInstance));
        m_InstanceLodStream.Upload(*m_Device, frameIndex, "GPUScene Instance LOD Table", sizeof(GPULodInfo),
                                   m_InstanceLodStaging.Data(), m_InstanceLodStaging.Size() * sizeof(GPULodInfo));
        m_MaterialStream.Upload(*m_Device, frameIndex, "GPUScene Material Buffer", sizeof(GPUMaterial),
                                m_MaterialStaging.Data(), m_MaterialStaging.Size() * sizeof(GPUMaterial));
        UploadSchemeParams(frameIndex);
    }
}

void GPUScene::BuildBuckets()
{
    m_Batches.Clear();
    m_Buckets.Clear();

    for (uint32 i = 0; i < (uint32)m_Draws.Size(); )
    {
        uint32 schemeId = m_Draws[i].schemeId;

        SchemeBucket bucket;
        bucket.schemeId = schemeId;
        bucket.firstBatch = (uint32)m_Batches.Size();

        while (i < (uint32)m_Draws.Size() && m_Draws[i].schemeId == schemeId)
        {
            uint32 materialSlot = m_Draws[i].materialSlot;

            MaterialBatch batch;
            batch.materialSlot = materialSlot;
            batch.firstDraw = i;
            while (i < (uint32)m_Draws.Size() && m_Draws[i].schemeId == schemeId && m_Draws[i].materialSlot == materialSlot)
                ++i;
            batch.drawCount = i - batch.firstDraw;

            m_Batches.PushBack(batch);
        }

        bucket.batchCount = (uint32)m_Batches.Size() - bucket.firstBatch;
        m_Buckets.PushBack(bucket);
    }
}

void GPUScene::UploadSchemeParams(uint32 frameIndex)
{
    for (uint32 schemeId = 0; schemeId < (uint32)m_SchemeParamStreams.Size(); ++schemeId)
    {
        const MaterialScheme& scheme = m_Schemes->Get(schemeId);
        if (scheme.paramStride == 0)
            continue;

        TArray<uint8>& staging = m_SchemeParamStaging[schemeId];
        if (staging.IsEmpty())
            continue;

        m_SchemeParamStreams[schemeId].Upload(*m_Device, frameIndex, scheme.name.CStr(), scheme.paramStride,
                                              staging.Data(), staging.Size());
    }
}
