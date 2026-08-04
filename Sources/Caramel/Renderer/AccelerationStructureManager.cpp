/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "AccelerationStructureManager.hpp"

#include <Caramel/Core/Logger.hpp>
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

    for (uint64 i = 0; i < FRAMES_IN_FLIGHT; ++i)
    {
        m_FenceFrameSlots[i] = 0;
        m_CommandBuffers[i] = device.CreateCommandBuffer(m_ComputeQueue);
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
    m_Fence.Wait(m_FenceFrameSlots[frameSlot]);
}

agfx::CommandBuffer& AccelerationStructureManager::GetFrameCommandBuffer(uint64 frameSlot)
{
    return m_CommandBuffers[frameSlot];
}

void AccelerationStructureManager::Submit(agfx::CommandBuffer& commandBuffer, uint64 frameSlot)
{
    m_ComputeQueue.Submit(commandBuffer);
    m_FenceFrameSlots[frameSlot] = ++m_FenceValue;
    m_ComputeQueue.Signal(m_Fence, m_FenceValue);
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
    TArray<agfxAccelerationStructureInstance> pendingInstances;
    pendingInstances.Reserve(renderInstances.Size());

    for (const RenderInstance& instance : renderInstances)
    {
        StreamingModel* model = instance.mesh;
        if (!model)
            continue;

        auto it = m_BLASEntries.Find(model);
        if (it == m_BLASEntries.End() || !it->second.ready)
            continue;

        // glm is column-major by default; AGFX wants a row-major 3x4.
        glm::mat4 t = glm::transpose(instance.transform);
        float rowMajor[12] = {
            t[0][0], t[0][1], t[0][2], t[0][3],
            t[1][0], t[1][1], t[1][2], t[1][3],
            t[2][0], t[2][1], t[2][2], t[2][3],
        };

        agfx::AccelerationStructureInstance inst;
        inst.SetBLAS(it->second.blas.Get()).SetTransform(rowMajor).SetUserID(instance.instanceIndex).SetOpaque(true);
        pendingInstances.PushBack(inst);
    }

    m_LastTLASInstanceCount = (uint32)pendingInstances.Size();

    if (pendingInstances.Size() > m_MaxInstanceCount)
    {
        if (!m_TLASNeedsGrow)
            CARAMEL_WARN("AccelerationStructureManager: TLAS instance count {} exceeds capacity {}, growing next frame", pendingInstances.Size(), m_MaxInstanceCount);
        m_TLASNeedsGrow = true;
        pendingInstances.Resize(m_MaxInstanceCount);
    }

    m_TLAS.ResetInstances();
    if (!pendingInstances.IsEmpty())
        m_TLAS.AddInstances(pendingInstances.Data(), (uint32)pendingInstances.Size());

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
