/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Scene/RenderInstance.hpp>

#include <AGFX/agfx.hpp>

class StreamingModel;

// Builds a BLAS per streamed mesh the frame its finest LOD becomes resident, on its own async
// compute queue, then rebuilds a single scene TLAS from whichever BLASes are ready every frame.
// No shading pass consumes the TLAS yet -- see Renderer's OverlayPanel stat block for verification.
class AccelerationStructureManager
{
public:
    explicit AccelerationStructureManager(agfx::Device& device);

    bool IsSupported() const { return m_RayTracingSupported; }

    void WaitForFrameSlot(uint64 frameSlot);
    agfx::CommandBuffer& GetFrameCommandBuffer(uint64 frameSlot);
    void Submit(agfx::CommandBuffer& commandBuffer, uint64 frameSlot);

    // CPU-only: decides which models need a BLAS this frame. Call before recording anything.
    void ScanForNewlyResidentModels(const TArray<RenderInstance>& renderInstances);

    // Records this frame's pending BLAS builds and the TLAS rebuild into `cmd` (the compute queue's
    // command buffer for this frame slot, already Begin()'d by the caller).
    void RecordBuilds(agfx::CommandBuffer& cmd, const TArray<RenderInstance>& renderInstances);

    uint32 GetTrackedModelCount() const { return (uint32)m_BLASEntries.Size(); }
    uint32 GetReadyBLASCount() const;
    uint32 GetLastTLASInstanceCount() const { return m_LastTLASInstanceCount; }

private:
    struct BLASEntry
    {
        agfx::AccelerationStructure blas;
        agfx::Buffer scratchBuffer;
        bool ready = false;
    };

    void EnsureTLASCapacity(uint32 requiredInstanceCount);
    void RecordBLASBuilds(agfx::CommandBuffer& cmd);
    void RecordTLASBuild(agfx::CommandBuffer& cmd, const TArray<RenderInstance>& renderInstances);

    agfx::Device* m_Device;
    bool m_RayTracingSupported = false;

    agfx::CommandQueue m_ComputeQueue;
    agfx::Fence m_Fence;
    uint64 m_FenceValue = 0;
    uint64 m_FenceFrameSlots[FRAMES_IN_FLIGHT] = {};
    agfx::CommandBuffer m_CommandBuffers[FRAMES_IN_FLIGHT];

    TDictionary<StreamingModel*, BLASEntry> m_BLASEntries;

    agfx::AccelerationStructure m_TLAS;
    agfx::Buffer m_TLASScratchBuffer;
    uint32 m_MaxInstanceCount = 256;
    bool m_TLASNeedsGrow = false;
    uint32 m_LastTLASInstanceCount = 0;
};
