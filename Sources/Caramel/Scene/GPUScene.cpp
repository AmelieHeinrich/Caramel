/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 10:05:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "GPUScene.hpp"

#include <Caramel/Asset/StreamingManager.hpp>
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
    m_InstanceStaging.Clear();
    m_Draws.Clear();

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

        // Redundant when several meshes share a slot, but it is 80 bytes and keeps the volatile
        // texture handles fresh without any dirty tracking.
        WriteMaterial(materialSlot, model.GetMaterial(), activeOverride, streamingManager,
                      model.GetRequestId(), model.GetMesh().materialIndex);

        const ModelMesh& mesh = model.GetMesh();

        GPUInstance gpu;
        gpu.transform = instance.transform;
        gpu.boundsMin = glm::vec4(mesh.boundsMin, 0.0f);
        gpu.boundsMax = glm::vec4(mesh.boundsMax, 0.0f);
        gpu.materialSlot = materialSlot;
        gpu.vertexBuffer = (uint32)model.GetVertexBufferView().GetHandle();
        gpu.meshletBuffer = (uint32)model.GetMeshletBufferView(lod).GetHandle();
        gpu.meshletVertexBuffer = (uint32)model.GetMeshletVertexBufferView(lod).GetHandle();
        gpu.meshletTriangleBuffer = (uint32)model.GetMeshletTriangleBufferView(lod).GetHandle();
        gpu.meshletCount = meshletCount;
        gpu.lod = lod;

        m_Draws.PushBack(GPUDraw{ (uint32)m_InstanceStaging.Size(), meshletCount, m_SlotSchemeId[materialSlot], materialSlot });
        m_InstanceStaging.PushBack(gpu);
    }

    // Sort draws into (scheme, material) order. The instance buffer is deliberately NOT reordered --
    // draws carry an index into it, so this stays a CPU-side reshuffle.
    std::sort(m_Draws.Begin(), m_Draws.End(), [](const GPUDraw& a, const GPUDraw& b) {
        if (a.schemeId != b.schemeId)
            return a.schemeId < b.schemeId;
        return a.materialSlot < b.materialSlot;
    });

    BuildBuckets();

    m_InstanceStream.Upload(*m_Device, frameIndex, "GPUScene Instance Buffer", sizeof(GPUInstance),
                            m_InstanceStaging.Data(), m_InstanceStaging.Size() * sizeof(GPUInstance));
    m_MaterialStream.Upload(*m_Device, frameIndex, "GPUScene Material Buffer", sizeof(GPUMaterial),
                            m_MaterialStaging.Data(), m_MaterialStaging.Size() * sizeof(GPUMaterial));
    UploadSchemeParams(frameIndex);
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
