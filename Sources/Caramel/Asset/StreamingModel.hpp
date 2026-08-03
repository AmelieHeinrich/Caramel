/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Asset/Model.hpp>
#include <Caramel/Asset/GPUModel.hpp>

#include <atomic>

class StreamingManager;

// Coarse-to-fine progressive LOD streaming for a single ModelMesh -- the mesh-LOD sibling of
// StreamingTexture, carrying the same pending-fence/PollCompletion contract (see
// StreamingManager.hpp). Mesh LODs are indexed ascending coarse (0) to fine (kLodCount - 1), the
// mirror image of texture mips (which descend coarse to fine), but the streaming policy is
// identical: at most one LOD in flight at a time, coarsest first, call RequestNextLOD() to advance.
class StreamingModel
{
public:
    static constexpr uint32 kNoResidentLOD = UINT32_MAX;

    // `source` is shared across every mesh of the same model (CPUModel just holds path + parsed
    // metadata, not a file handle, so sharing it avoids duplicating that metadata per submesh).
    // The shared vertex buffer is uploaded synchronously here, on the calling thread, rather than
    // via a JobSystem job like the mip/LOD loads below -- it must land before the first LOD's
    // meshlet-vertex indices can be dereferenced, and since BeginLoad/RequestNextLOD's uploads are
    // otherwise independent async jobs racing the same UploadQueue, doing it inline is the simplest
    // way to guarantee the ordering. This is a one-shot cost paid once per mesh when the model is
    // first loaded (a user-triggered action), not a per-frame one.
    void BeginLoad(TShared<CPUModel> source, uint32 meshIndex, StreamingManager& manager);

    // Schedules the next (finer) LOD if none is currently in flight and LODs remain. Returns false
    // if there was nothing to do (a LOD is already pending, or the finest LOD is already resident).
    bool RequestNextLOD(StreamingManager& manager);

    // Called once per frame by StreamingManager::Update() with the upload queue's fence's completed
    // value; promotes the pending LOD to resident once its upload has landed.
    void PollCompletion(uint64 completedFenceValue);

    const ModelMesh& GetMesh() const { return m_Source->GetMeshes()[m_MeshIndex]; }
    const glm::mat4& GetWorldTransform() const { return m_WorldTransform; }

    uint32 SnapshotResidentLOD() const { return m_HighestResidentLOD.load(std::memory_order_acquire); }
    bool HasPendingLOD() const { return m_UploadInFlight.load(std::memory_order_acquire); }

    uint32 GetMeshletCount(uint32 lod) const { return GetMesh().lods[lod].meshletCount; }

    agfx::BufferView& GetVertexBufferView() { return m_Gpu.GetVertexBufferView(); }
    agfx::BufferView& GetMeshletBufferView(uint32 lod) { return m_Gpu.GetMeshletBufferView(lod); }
    agfx::BufferView& GetMeshletVertexBufferView(uint32 lod) { return m_Gpu.GetMeshletVertexBufferView(lod); }
    agfx::BufferView& GetMeshletTriangleBufferView(uint32 lod) { return m_Gpu.GetMeshletTriangleBufferView(lod); }

private:
    void OnLODResident(uint32 lodIndex);

    TShared<CPUModel> m_Source;
    uint32 m_MeshIndex = 0;
    glm::mat4 m_WorldTransform{ 1.0f };

    GPUModel m_Gpu;

    std::atomic<uint32> m_HighestResidentLOD{ kNoResidentLOD };

    // Cursor walking the LOD table forwards (coarsest to finest). kNoResidentLOD once the finest
    // LOD has been requested.
    uint32 m_NextLodToLoad = 0;

    // Claimed synchronously by RequestNextLOD, released by PollCompletion once the LOD is resident.
    // m_PendingFenceValue cannot serve as the in-flight flag: the job publishes it only after the
    // (slow) disk read, so between RequestNextLOD returning and that store the mesh would look idle
    // and a second LOD would be scheduled concurrently -- two jobs then race to write
    // m_PendingFenceValue/m_PendingLOD, and the loser's stale pair can promote a LOD whose upload
    // has not landed, or walk the resident LOD backwards.
    std::atomic<bool> m_UploadInFlight{ false };

    std::atomic<uint64> m_PendingFenceValue{ 0 }; // 0 == fence not published yet
    std::atomic<uint32> m_PendingLOD{ kNoResidentLOD };
};
