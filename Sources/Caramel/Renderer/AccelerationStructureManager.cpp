/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "AccelerationStructureManager.hpp"

#include <Caramel/Core/Logger.hpp>
#include <Caramel/Core/JobSystem.hpp>
#include <Caramel/Asset/StreamingModel.hpp>

#include <glm/glm.hpp>

#if defined(CARAMEL_WINDOWS)
    #undef MemoryBarrier
#endif

AccelerationStructureManager::AccelerationStructureManager(agfx::Device& device)
    : m_Device(&device)
{
    agfxDeviceInfo info = device.GetInfo();
    m_RayTracingSupported = info.supportsRayTracing;

    if (!m_RayTracingSupported)
    {
        CARAMEL_WARN("AccelerationStructureManager: device does not support raytracing -- BLAS/TLAS building disabled");
        return;
    }

    m_ComputeQueue = device.CreateCommandQueue(agfx::CommandQueueType::Compute);
    m_Fence = device.CreateFence();

    agfx::QueryPoolCreateInfo timingQueryPoolInfo = agfx::QueryPoolCreateInfo().SetCount(RenderGraph::kMaxTimedPasses * 2);
    for (uint64 i = 0; i < FRAMES_IN_FLIGHT; ++i)
    {
        m_CommandBuffers[i] = device.CreateCommandBuffer(m_ComputeQueue);
        m_TimingQueryPools[i] = device.CreateQueryPool(m_ComputeQueue, timingQueryPoolInfo);
    }

    EnsureTLASCapacity(m_MaxInstanceCount);
}

void AccelerationStructureManager::EnsureTLASCapacity(uint32 requiredInstanceCount)
{
    m_MaxInstanceCount = requiredInstanceCount;

    m_TLAS = m_Device->CreateAccelerationStructure(
        agfx::AccelerationStructureCreateInfo().SetTopLevel(m_MaxInstanceCount).SetName("Scene TLAS"));

    agfxAccelerationStructureSizes sizes = m_TLAS.GetSizes();
    m_TLASScratchBuffer = m_Device->CreateBuffer(
        agfx::BufferCreateInfo().SetSize(sizes.scratchBufferSize)
                                 .SetUsage(agfx::BufferUsage::ShaderWrite)
                                 .SetMemoryType(agfx::BufferMemoryType::GPUOnly));

    m_Device->MakeResourcesResident();
    m_TLASNeedsGrow = false;
}

void AccelerationStructureManager::WaitForFrameSlot(uint64 frameSlot)
{
    if (!m_RayTracingSupported)
        return;
    m_Fence.Wait(m_FenceValue);

    if (m_TimingSlotHasData[frameSlot] && !m_TimingSlotNames[frameSlot].IsEmpty())
    {
        uint32 count = (uint32)m_TimingSlotNames[frameSlot].Size();
        uint64 timestamps[RenderGraph::kMaxTimedPasses * 2];
        m_TimingQueryPools[frameSlot].Readback(0, count * 2, timestamps);

        m_LastPassTimings.Clear();
        for (uint32 i = 0; i < count; ++i)
        {
            float gpuTimeMs = (float)(timestamps[i * 2 + 1] - timestamps[i * 2]) / 1000000.0f;
            m_LastPassTimings.PushBack({ m_TimingSlotNames[frameSlot][i], gpuTimeMs });
        }
    }
}

agfx::CommandBuffer& AccelerationStructureManager::GetFrameCommandBuffer(uint64 frameSlot)
{
    return m_CommandBuffers[frameSlot];
}

void AccelerationStructureManager::Submit(agfx::CommandBuffer& commandBuffer)
{
    m_ComputeQueue.Submit(commandBuffer);
    m_ComputeQueue.Signal(m_Fence, ++m_FenceValue);
}

void AccelerationStructureManager::ScanForNewlyResidentModels(const TArray<RenderInstance>& renderInstances)
{
    if (!m_RayTracingSupported)
        return;

    for (const RenderInstance& instance : renderInstances)
    {
        StreamingModel* model = instance.mesh;
        if (!model || m_BLASEntries.Contains(model))
            continue;

        // One-time trigger, finest LOD only -- never rebuilt on later LOD promotion. Array index
        // kLodCount-1 is the highest-detail mesh (see MeshCompressor::Compile: lod0Indices is stored
        // at mesh.lods[kLodCount-1], coarser simplifications fill indices below it), and
        // StreamingModel streams index 0 (coarsest) in first, ascending toward kLodCount-1 -- so this
        // fires on the *last* streaming milestone, not the first.
        constexpr uint32 kFinestLOD = CaramelAsset::kLodCount - 1;
        if (model->SnapshotResidentLOD() != kFinestLOD)
            continue;

        agfx::AccelerationStructureGeometry geometry;
        geometry.SetOpaque(true)
                .SetTriangles(model->GetVertexBuffer().Get(), model->GetMesh().vertexCount,
                              model->GetFlatIndexBuffer(kFinestLOD).Get(), model->GetFlatIndexCount(kFinestLOD));

        BLASEntry entry;
        entry.blas = m_Device->CreateAccelerationStructure(
            agfx::AccelerationStructureCreateInfo().SetBottomLevel(&geometry, 1).SetName("BLAS"));

        agfxAccelerationStructureSizes sizes = entry.blas.GetSizes();
        entry.scratchBuffer = m_Device->CreateBuffer(
            agfx::BufferCreateInfo().SetSize(sizes.scratchBufferSize)
                                     .SetUsage(agfx::BufferUsage::ShaderWrite)
                                     .SetMemoryType(agfx::BufferMemoryType::GPUOnly));

        m_Device->MakeResourcesResident();

        m_BLASEntries.emplace(model, std::move(entry));
    }
}

void AccelerationStructureManager::RecordBLASBuilds(agfx::CommandBuffer& cmd)
{
    bool builtAny = false;
    {
        agfx::ComputePass pass = cmd.BeginComputePass("BLAS Build");
        for (auto& [model, entry] : m_BLASEntries)
        {
            if (entry.ready)
                continue;
            pass.BuildAccelerationStructure(entry.blas, entry.scratchBuffer);
            entry.ready = true;
            builtAny = true;
        }
    }

    if (builtAny)
        cmd.MemoryBarrier(agfx::ResourceState::RaytracingAccelerationStructure, agfx::ResourceState::RaytracingAccelerationStructure, true);
}

void AccelerationStructureManager::RecordTLASBuild(agfx::CommandBuffer& cmd, const TArray<RenderInstance>& renderInstances)
{
    uint32 candidateCount = (uint32)renderInstances.Size();
    m_PendingInstances.Resize(candidateCount);

    // m_BLASEntries is read-only here -- registration and ready flags were finalized earlier this
    // frame (ScanForNewlyResidentModels / RecordBLASBuilds) -- so the lookups can run in parallel.
    // Instances without a ready BLAS are marked with a null blas and compacted out below.
    JobSystem::Get().ParallelFor(candidateCount, 256, [&](uint32 start, uint32 end, uint32) {
        for (uint32 i = start; i < end; ++i)
        {
            const RenderInstance& instance = renderInstances[i];
            agfx::AccelerationStructureInstance& inst = m_PendingInstances[i];

            StreamingModel* model = instance.mesh;
            auto it = model ? m_BLASEntries.Find(model) : m_BLASEntries.End();
            if (it == m_BLASEntries.End() || !it->second.ready)
            {
                inst.blas = nullptr;
                continue;
            }

            // glm is column-major by default; AGFX wants a row-major 3x4.
            glm::mat4 t = glm::transpose(instance.transform);
            float rowMajor[12] = {
                t[0][0], t[0][1], t[0][2], t[0][3],
                t[1][0], t[1][1], t[1][2], t[1][3],
                t[2][0], t[2][1], t[2][2], t[2][3],
            };

            inst.SetBLAS(it->second.blas.Get()).SetTransform(rowMajor).SetUserID(instance.instanceIndex).SetOpaque(true);
        }
    });

    uint32 liveCount = 0;
    for (uint32 i = 0; i < candidateCount; ++i)
    {
        if (!m_PendingInstances[i].blas)
            continue;
        if (liveCount != i)
            m_PendingInstances[liveCount] = m_PendingInstances[i];
        ++liveCount;
    }

    m_LastTLASInstanceCount = liveCount;

    if (liveCount > m_MaxInstanceCount)
    {
        if (!m_TLASNeedsGrow)
            CARAMEL_WARN("AccelerationStructureManager: TLAS instance count {} exceeds capacity {}, growing next frame", liveCount, m_MaxInstanceCount);
        m_TLASNeedsGrow = true;
        liveCount = m_MaxInstanceCount;
    }

    m_TLAS.ResetInstances();
    if (liveCount > 0)
        m_TLAS.AddInstances(m_PendingInstances.Data(), liveCount);

    {
        agfx::ComputePass pass = cmd.BeginComputePass("TLAS Build");
        pass.BuildAccelerationStructure(m_TLAS, m_TLASScratchBuffer);
    }

    cmd.MemoryBarrier(agfx::ResourceState::RaytracingAccelerationStructure, agfx::ResourceState::RaytracingAccelerationStructure, true);
}

void AccelerationStructureManager::RecordBuilds(agfx::CommandBuffer& cmd, const TArray<RenderInstance>& renderInstances)
{
    if (!m_RayTracingSupported)
        return;

    if (m_TLASNeedsGrow)
        EnsureTLASCapacity(m_MaxInstanceCount * 2);

    RecordBLASBuilds(cmd);
    RecordTLASBuild(cmd, renderInstances);
}

uint32 AccelerationStructureManager::GetReadyBLASCount() const
{
    uint32 count = 0;
    for (const auto& [model, entry] : m_BLASEntries)
        if (entry.ready)
            count++;
    return count;
}
