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
#include <cstring>

namespace
{
    uint64 BaseSlotKey(uint32 requestId, int32 materialIndex)
    {
        return ((uint64)requestId << 32) | (uint64)(uint32)materialIndex;
    }

    // splitmix64's finalizer. The identity key packs node id, mesh slot and instance index into
    // disjoint bit ranges, so masking its low bits directly would pile every instance of one entity
    // into a handful of buckets.
    uint64 MixIdentity(uint64 x)
    {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ull;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebull;
        x ^= x >> 31;
        return x;
    }

    uint64 InstanceIdentity(const RenderInstance& instance)
    {
        // Field widths matter: this keys the state slot, so two instances colliding here share one
        // slot and therefore one meshlet-visibility bit range, which ORs their visible sets together
        // and quietly disables meshlet culling for both. The old 12-bit instanceIndex field
        // overflowed into meshSlot past 4096 instances under one node.
        return instance.owner
            ? (((uint64)instance.owner->id << 40) | ((uint64)instance.meshSlot << 24) | instance.instanceIndex)
            : (((uint64)(uintptr_t)instance.mesh << 24) | instance.instanceIndex);
    }
}

void GPUScene::Init(agfx::Device& device, agfx::CommandQueue& queue, const SchemeRegistry& schemes, uint32 framesInFlight)
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

    // Fill it with opaque white. A freshly created texture holds whatever its allocation happened to
    // contain -- in practice zeros, i.e. black -- and every material with a missing or not-yet-
    // resident texture samples this and multiplies it into its factor. Black meant an untextured
    // material shaded as pure black however its baseColorFactor was set, and a missing metallic/
    // roughness map read as fully rough dielectric rather than as "use the factors".
    //
    // Done here with a one-shot submit rather than through UploadQueue: this runs once at startup,
    // before the streaming manager exists, and nothing may sample the texture until it lands.
    // agfxTextureReplaceRegion is not an option -- it is a no-op on D3D12 (UMA only).
    {
        const uint8 white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

        agfx::BufferCreateInfo stagingInfo;
        stagingInfo.SetSize(sizeof(white))
                   .SetStride(sizeof(white))
                   .SetUsage(agfx::BufferUsage::ShaderRead)
                   .SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        agfx::Buffer staging = m_Device->CreateBuffer(stagingInfo);
        staging.SetName("GPUScene Fallback Texture Staging");
        m_Device->MakeResourcesResident();

        {
            agfx::MappedBuffer mapped(staging);
            std::memcpy(mapped.Get(), white, sizeof(white));
        }

        agfx::CommandBuffer cmd = m_Device->CreateCommandBuffer(queue);
        cmd.Begin();
        cmd.TextureBarrier(m_FallbackTexture, agfx::ResourceState::Common, agfx::ResourceState::CopyDest, 0, 0);
        {
            agfx::TextureRegion region;
            region.SetSize(1, 1);
            agfx::ComputePass pass = cmd.BeginComputePass("Fallback Texture Upload");
            pass.CopyBufferToTexture(staging, 0, m_FallbackTexture, region, 0, 0, sizeof(white), sizeof(white));
        }
        cmd.TextureBarrier(m_FallbackTexture, agfx::ResourceState::CopyDest, agfx::ResourceState::PixelShaderResource, 0, 0);
        cmd.End();

        queue.Submit(cmd);

        // The staging buffer dies with this scope, so the copy has to have completed. Nothing is
        // rendering yet, so blocking here costs nothing.
        agfx::Fence fence = m_Device->CreateFence();
        queue.Signal(fence, 1);
        fence.Wait(1);
    }
}

MaterialOverride* GPUScene::FindActiveOverride(const RenderInstance& instance) const
{
    if (!instance.owner)
        return nullptr;

    int32 materialIndex = instance.mesh->GetMesh().materialIndex;

    MaterialOverride* chosen = nullptr;
    for (MaterialOverride& matOverride : instance.owner->materialOverrides)
    {
        if (matOverride.materialIndex != materialIndex || !matOverride.HasAnyOverride())
            continue;

        if (matOverride.meshSlot == (int32)instance.meshSlot)
            return &matOverride;

        if (matOverride.meshSlot == MaterialOverride::kAllMeshes)
            chosen = &matOverride;
    }

    return chosen;
}

uint32 GPUScene::LookupMaterialSlot(const RenderInstance& instance) const
{
    // An override with no slot yet holds UINT32_MAX, which is exactly the "not resolved" sentinel
    // the caller is looking for.
    if (const MaterialOverride* chosen = FindActiveOverride(instance))
        return chosen->gpuMaterialSlot;

    uint64 key = BaseSlotKey(instance.mesh->GetRequestId(), instance.mesh->GetMesh().materialIndex);
    auto it = m_BaseSlots.Find(key);
    return it != m_BaseSlots.End() ? it->second : UINT32_MAX;
}

uint32 GPUScene::ResolveMaterialSlot(const RenderInstance& instance, const MaterialOverride*& outOverride)
{
    outOverride = nullptr;

    // An entity that overrides this material gets its own derived slot, so two entities instanced
    // from the same model no longer share (and clobber) each other's factors.
    if (MaterialOverride* chosen = FindActiveOverride(instance))
    {
        if (chosen->gpuMaterialSlot == UINT32_MAX)
            chosen->gpuMaterialSlot = m_NextMaterialSlot++;

        outOverride = chosen;
        return chosen->gpuMaterialSlot;
    }

    uint64 key = BaseSlotKey(instance.mesh->GetRequestId(), instance.mesh->GetMesh().materialIndex);
    auto it = m_BaseSlots.Find(key);
    if (it != m_BaseSlots.End())
        return it->second;

    uint32 slot = m_NextMaterialSlot++;
    m_BaseSlots[key] = slot;
    return slot;
}

uint32 GPUScene::FindState(uint64 identity) const
{
    if (m_StateIndex.IsEmpty())
        return kNoState;

    uint64 mask = m_StateIndex.Size() - 1;
    uint64 pos = MixIdentity(identity) & mask;
    while (true)
    {
        uint32 entry = m_StateIndex[pos];
        if (entry == kNoState || m_States[entry].identity == identity)
            return entry;

        pos = (pos + 1) & mask;
    }
}

void GPUScene::InsertStateIndex(uint64 identity, uint32 entry)
{
    uint64 mask = m_StateIndex.Size() - 1;
    uint64 pos = MixIdentity(identity) & mask;
    while (m_StateIndex[pos] != kNoState)
        pos = (pos + 1) & mask;

    m_StateIndex[pos] = entry;
}

void GPUScene::RehashStates()
{
    // Half load factor: linear probing degrades badly past that, and the table is one uint32 per
    // bucket, so the memory is not worth economising.
    uint64 capacity = 64;
    while (capacity < (m_States.Size() + 1) * 2)
        capacity *= 2;

    m_StateIndex.Clear();
    m_StateIndex.Resize(capacity, kNoState);

    for (uint32 i = 0; i < (uint32)m_States.Size(); ++i)
        InsertStateIndex(m_States[i].identity, i);
}

uint32 GPUScene::CreateState(uint64 identity, const glm::mat4& transform)
{
    uint32 stateSlot;
    if (!m_FreeStateSlots.IsEmpty())
    {
        stateSlot = m_FreeStateSlots[m_FreeStateSlots.Size() - 1];
        m_FreeStateSlots.PopBack();
    }
    else
    {
        stateSlot = m_StateSlotCount++;
    }

    uint32 entry = (uint32)m_States.Size();
    m_States.PushBack(InstanceState{ identity, transform, stateSlot });
    m_StateSeen.PushBack(m_BuildCounter);

    if ((m_States.Size() + 1) * 2 > m_StateIndex.Size())
        RehashStates();
    else
        InsertStateIndex(identity, entry);

    return entry;
}

void GPUScene::SweepDeadStates()
{
    // The common case is that nothing vanished, and answering that costs one pass over the frame
    // stamps -- 4 bytes per entry -- instead of compacting and rehashing.
    uint32 alive = 0;
    for (uint32 seen : m_StateSeen)
        alive += (seen == m_BuildCounter) ? 1u : 0u;

    if (alive == (uint32)m_States.Size())
        return;

    uint32 write = 0;
    for (uint32 read = 0; read < (uint32)m_States.Size(); ++read)
    {
        if (m_StateSeen[read] != m_BuildCounter)
        {
            m_FreeStateSlots.PushBack(m_States[read].stateSlot);
            continue;
        }

        if (write != read)
        {
            m_States[write] = m_States[read];
            m_StateSeen[write] = m_StateSeen[read];
        }
        ++write;
    }

    m_States.Resize(write);
    m_StateSeen.Resize(write);
    RehashStates();
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

    ++m_BuildCounter;

    m_PreCullMeshlets = 0;
    m_PreCullTriangles = 0;
    m_MaxMeshletCount = 0;
    m_MaterialWrites.Clear();

    uint32 instanceCount = (uint32)renderInstances.Size();
    m_BuildItems.Resize(instanceCount);
    m_ItemFlags.Clear();
    m_ItemFlags.Resize(instanceCount, 0);
    m_ItemSlot.Resize(instanceCount);
    m_ItemState.Resize(instanceCount);
    m_ItemDst.Resize(instanceCount);
    m_ItemDrawDst.Resize(instanceCount);

    m_Scratch.Clear();
    m_Scratch.Resize(JobSystem::Get().GetWorkerCount() + 1);

    {
        // Gather phase. Everything per-instance that can be done without mutating shared state:
        // residency filtering, the per-LOD meshlet scan, and *lookups* into the material-slot and
        // instance-state tables, both of which stay read-only for the whole phase. An instance whose
        // lookup misses leaves its sentinel in place and gets fixed up serially below -- in steady
        // state nothing misses, so the serial phase does no real work.
        //
        // Results are written at the render-instance index, not compacted, so the phases can run
        // over one fixed index space and no worker needs to know how many instances precede it.
        CARAMEL_ZONE("Gather Instances");
        JobSystem::Get().ParallelFor(instanceCount, 256, [&](uint32 start, uint32 end, uint32 threadIndex) {
            CARAMEL_ZONE("Gather Instances (chunk)");
            BuildScratch& scratch = m_Scratch[threadIndex];

            for (uint32 i = start; i < end; ++i)
            {
                const RenderInstance& instance = renderInstances[i];
                StreamingModel& model = *instance.mesh;

                uint32 lod = model.SnapshotResidentLOD();
                if (lod == StreamingModel::kNoResidentLOD)
                    continue;

                uint32 meshletCount = model.GetMeshletCount(lod);
                if (meshletCount == 0)
                    continue;

                const ModelMesh& mesh = model.GetMesh();
                for (uint32 lodIdx = 0; lodIdx < CaramelAsset::kLodCount; ++lodIdx)
                    scratch.maxMeshletCount = std::max(scratch.maxMeshletCount, mesh.lods[lodIdx].meshletCount);

                scratch.preCullMeshlets += meshletCount;
                scratch.preCullTriangles += mesh.lods[lod].GetFlatIndexCount() / 3;

                uint64 identity = InstanceIdentity(instance);

                m_BuildItems[i] = BuildItem{ &instance, identity, lod, meshletCount };
                m_ItemSlot[i] = LookupMaterialSlot(instance);
                m_ItemState[i] = FindState(identity);
                m_ItemFlags[i] = kItemValid;
            }
        });

        for (const BuildScratch& scratch : m_Scratch)
        {
            m_PreCullMeshlets += scratch.preCullMeshlets;
            m_PreCullTriangles += scratch.preCullTriangles;
            m_MaxMeshletCount = std::max(m_MaxMeshletCount, scratch.maxMeshletCount);
        }
    }

    uint32 itemCount = 0;
    {
        // The only serial pass over the instances, and it streams over the flat side arrays rather
        // than the item structs. It allocates whatever the gather phase could not (material slots
        // for materials seen for the first time, state entries for new identities), stamps state
        // liveness for the sweep at the end, and hands out compacted instance indices -- all in
        // render-instance order, so slot assignment stays deterministic.
        CARAMEL_ZONE("Resolve Misses");

        m_SlotWrittenThisBuild.Clear();
        m_SlotWrittenThisBuild.Resize(m_NextMaterialSlot, 0);
        m_SlotDrawCount.Clear();
        m_SlotDrawCount.Resize(m_NextMaterialSlot, 0);

        for (uint32 i = 0; i < instanceCount; ++i)
        {
            if (!(m_ItemFlags[i] & kItemValid))
                continue;

            const RenderInstance& instance = *m_BuildItems[i].instance;

            uint32 slot = m_ItemSlot[i];
            if (slot == UINT32_MAX)
            {
                const MaterialOverride* activeOverride = nullptr;
                slot = ResolveMaterialSlot(instance, activeOverride);
                m_ItemSlot[i] = slot;
            }

            if (slot >= m_MaterialStaging.Size())
            {
                m_MaterialStaging.Resize(slot + 1);
                m_SlotSchemeId.Resize(slot + 1, SchemeRegistry::kDefaultSchemeId);
            }
            if (slot >= m_SlotWrittenThisBuild.Size())
            {
                m_SlotWrittenThisBuild.Resize(slot + 1, 0);
                m_SlotDrawCount.Resize(slot + 1, 0);
            }

            if (!m_SlotWrittenThisBuild[slot])
            {
                m_SlotWrittenThisBuild[slot] = 1;
                m_MaterialWrites.PushBack(MaterialWrite{ slot, instance.mesh, FindActiveOverride(instance) });
            }
            ++m_SlotDrawCount[slot];

            uint32 state = m_ItemState[i];
            if (state == kNoState)
            {
                // Seeded with the current transform, so an instance seen for the first time (or
                // reappearing after being evicted) reports zero object motion rather than a jump.
                state = CreateState(m_BuildItems[i].identity, instance.transform);
                m_ItemState[i] = state;
                m_ItemFlags[i] = (uint8)(m_ItemFlags[i] | kItemStateFresh);
            }
            m_StateSeen[state] = m_BuildCounter;

            m_ItemDst[i] = itemCount++;
        }
    }

    {
        // One WriteMaterial per slot per frame instead of one per instance -- same freshness for the
        // volatile texture handles, a fraction of the scheme-param packing and streaming lookups.
        CARAMEL_ZONE("Write Materials");
        for (const MaterialWrite& write : m_MaterialWrites)
            WriteMaterial(write.slot, write.model->GetMaterial(), write.activeOverride, streamingManager,
                          write.model->GetRequestId(), write.model->GetMesh().materialIndex);
    }

    m_InstanceStaging.Resize(itemCount);
    m_InstanceLodStaging.Resize((uint64)itemCount * CaramelAsset::kLodCount);
    m_Draws.Resize(itemCount);

    {
        // Draw ordering. BuildBuckets sorts the used *slots* (a handful) into (scheme, material)
        // order and gives each one its base offset; assigning each instance the next index inside
        // its slot's range then produces the same list the old std::sort over every draw did.
        CARAMEL_ZONE("Order Draws");
        BuildBuckets();

        for (uint32 i = 0; i < instanceCount; ++i)
        {
            if (!(m_ItemFlags[i] & kItemValid))
                continue;

            m_ItemDrawDst[i] = m_SlotDrawCursor[m_ItemSlot[i]]++;
        }
    }

    // Parallel fill: every write below lands at an index owned by exactly one item, and everything
    // read is either per-item or immutable for the rest of this Build.
    {
        CARAMEL_ZONE("Parallel Fill (dispatch)");
        JobSystem::Get().ParallelFor(instanceCount, 256, [&](uint32 start, uint32 end, uint32) {
            CARAMEL_ZONE("Parallel Fill (chunk)");
            for (uint32 i = start; i < end; ++i)
            {
                uint8 flags = m_ItemFlags[i];
                if (!(flags & kItemValid))
                    continue;

                const BuildItem& item = m_BuildItems[i];
                StreamingModel& model = *item.instance->mesh;
                const ModelMesh& mesh = model.GetMesh();

                uint32 dst = m_ItemDst[i];
                uint32 materialSlot = m_ItemSlot[i];
                InstanceState& state = m_States[m_ItemState[i]];

                GPUInstance gpu;
                gpu.transform = item.instance->transform;
                gpu.boundsMin = glm::vec4(mesh.boundsMin, 0.0f);
                gpu.boundsMax = glm::vec4(mesh.boundsMax, 0.0f);
                gpu.materialSlot = materialSlot;
                gpu.vertexBuffer = (uint32)model.GetVertexBufferView().GetHandle();
                gpu.meshletBuffer = (uint32)model.GetMeshletBufferView(item.lod).GetHandle();
                gpu.meshletVertexBuffer = (uint32)model.GetMeshletVertexBufferView(item.lod).GetHandle();
                gpu.meshletTriangleBuffer = (uint32)model.GetMeshletTriangleBufferView(item.lod).GetHandle();
                gpu.meshletCount = item.meshletCount;
                gpu.lod = item.lod;
                gpu.meshletBoundsBuffer = (uint32)model.GetMeshletBoundsBufferView(item.lod).GetHandle();
                gpu.prevTransform = state.prevTransform;
                gpu.stateSlot = state.stateSlot;
                gpu.stateFresh = (flags & kItemStateFresh) ? 1u : 0u;

                // Columns, matching the shader's mul(M, axis) convention.
                float32 sx = glm::length(glm::vec3(gpu.transform[0]));
                float32 sy = glm::length(glm::vec3(gpu.transform[1]));
                float32 sz = glm::length(glm::vec3(gpu.transform[2]));
                gpu.boundsScaleMax = std::max(sx, std::max(sy, sz));
                gpu.boundsScaleMin = std::min(sx, std::min(sy, sz));

                m_InstanceStaging[dst] = gpu;

                // Rolled forward in place rather than into a map rebuilt every frame. The entry
                // belongs to exactly one item, so this write is unshared; entries whose instance
                // vanished are dropped by SweepDeadStates below, which is what gives a reappearing
                // instance prev = current instead of a stale transform.
                state.prevTransform = gpu.transform;

                // Every LOD's handles + CPU-known meshlet count, not just the resident one baked into
                // `gpu` above -- PopulateOpaqueIndirectBundleCS picks the actual LOD per instance per
                // frame from this. Entry dst's LODs occupy [dst * kLodCount, (dst + 1) * kLodCount).
                for (uint32 lodIdx = 0; lodIdx < CaramelAsset::kLodCount; ++lodIdx)
                {
                    GPULodInfo lodInfo;
                    lodInfo.meshletBuffer = (uint32)model.GetMeshletBufferView(lodIdx).GetHandle();
                    lodInfo.meshletVertexBuffer = (uint32)model.GetMeshletVertexBufferView(lodIdx).GetHandle();
                    lodInfo.meshletTriangleBuffer = (uint32)model.GetMeshletTriangleBufferView(lodIdx).GetHandle();
                    lodInfo.meshletBoundsBuffer = (uint32)model.GetMeshletBoundsBufferView(lodIdx).GetHandle();
                    lodInfo.meshletCount = mesh.lods[lodIdx].meshletCount;
                    m_InstanceLodStaging[(uint64)dst * CaramelAsset::kLodCount + lodIdx] = lodInfo;
                }

                m_Draws[m_ItemDrawDst[i]] = GPUDraw{ dst, item.meshletCount, m_SlotSchemeId[materialSlot], materialSlot };
            }
        });
    }

    {
        // Recycle the slots of instances that stopped being submitted (streamed out, despawned).
        // They are handed back out marked fresh, so the next owner never inherits their state. Runs
        // last because compacting m_States moves entries the fill phase was still indexing into.
        CARAMEL_ZONE("Recycle State Slots");
        SweepDeadStates();

        // Fewer live entries than instances means two instances hashed to the same identity and are
        // now sharing persistent state -- silent apart from culling quietly degrading, so say so.
        if ((uint32)m_States.Size() < itemCount && !m_WarnedStateSlotCollision)
        {
            m_WarnedStateSlotCollision = true;
            CARAMEL_WARN("GPUScene: {} instances share only {} state slots -- identity collision, "
                         "per-instance LOD and visibility state is being shared",
                         itemCount, m_States.Size());
        }
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

    // m_MaterialWrites already holds every slot used this frame, exactly once.
    m_OrderedSlots.Clear();
    m_OrderedSlots.Reserve(m_MaterialWrites.Size());
    for (const MaterialWrite& write : m_MaterialWrites)
        m_OrderedSlots.PushBack(write.slot);

    std::sort(m_OrderedSlots.Begin(), m_OrderedSlots.End(), [this](uint32 a, uint32 b) {
        if (m_SlotSchemeId[a] != m_SlotSchemeId[b])
            return m_SlotSchemeId[a] < m_SlotSchemeId[b];
        return a < b;
    });

    m_SlotDrawCursor.Clear();
    m_SlotDrawCursor.Resize(m_SlotDrawCount.Size(), 0);

    uint32 firstDraw = 0;
    for (uint32 slot : m_OrderedSlots)
    {
        uint32 schemeId = m_SlotSchemeId[slot];
        if (m_Buckets.IsEmpty() || m_Buckets[m_Buckets.Size() - 1].schemeId != schemeId)
            m_Buckets.PushBack(SchemeBucket{ schemeId, (uint32)m_Batches.Size(), 0 });

        m_Batches.PushBack(MaterialBatch{ slot, firstDraw, m_SlotDrawCount[slot] });
        ++m_Buckets[m_Buckets.Size() - 1].batchCount;

        m_SlotDrawCursor[slot] = firstDraw;
        firstDraw += m_SlotDrawCount[slot];
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
