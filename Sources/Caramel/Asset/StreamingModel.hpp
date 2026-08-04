/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Asset/Model.hpp>
#include <Caramel/Asset/GPUModel.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <atomic>

class StreamingManager;

class StreamingModel
{
public:
    static constexpr uint32 kNoResidentLOD = UINT32_MAX;

    void BeginLoad(TShared<CPUModel> source, uint32 meshIndex, StreamingManager& manager, uint32 requestId);

    bool RequestNextLOD(StreamingManager& manager);

    uint64 PollCompletion(uint64 completedFenceValue);

    uint64 GetNextUploadBytes() const;

    const ModelMesh& GetMesh() const { return m_Source->GetMeshes()[m_MeshIndex]; }
    const glm::mat4& GetWorldTransform() const { return m_WorldTransform; }
    uint32 GetRequestId() const { return m_RequestId; }

    const ModelMaterial& GetMaterial() const;

    TShared<CPUModel> GetSourceModel() const { return m_Source; }

    uint32 SnapshotResidentLOD() const { return m_HighestResidentLOD.load(std::memory_order_acquire); }
    bool HasPendingLOD() const { return m_UploadInFlight.load(std::memory_order_acquire); }

    uint32 GetMeshletCount(uint32 lod) const { return GetMesh().lods[lod].meshletCount; }

    bool HasCollider() const { return m_ColliderReady.load(std::memory_order_acquire); }
    JPH::RefConst<JPH::Shape> GetColliderShape() const { return m_ColliderShape; }

    agfx::BufferView& GetVertexBufferView() { return m_Gpu.GetVertexBufferView(); }
    agfx::BufferView& GetMeshletBufferView(uint32 lod) { return m_Gpu.GetMeshletBufferView(lod); }
    agfx::BufferView& GetMeshletVertexBufferView(uint32 lod) { return m_Gpu.GetMeshletVertexBufferView(lod); }
    agfx::BufferView& GetMeshletTriangleBufferView(uint32 lod) { return m_Gpu.GetMeshletTriangleBufferView(lod); }

private:
    void OnLODResident(uint32 lodIndex);

    TShared<CPUModel> m_Source;
    uint32 m_MeshIndex = 0;
    uint32 m_RequestId = 0;
    glm::mat4 m_WorldTransform{ 1.0f };

    GPUModel m_Gpu;

    std::atomic<uint32> m_HighestResidentLOD{ kNoResidentLOD };

    uint32 m_NextLodToLoad = 0;

    std::atomic<bool> m_UploadInFlight{ false };

    std::atomic<uint64> m_PendingFenceValue{ 0 };
    std::atomic<uint32> m_PendingLOD{ kNoResidentLOD };

    JPH::RefConst<JPH::Shape> m_ColliderShape;
    std::atomic<bool> m_ColliderReady{ false };
};
