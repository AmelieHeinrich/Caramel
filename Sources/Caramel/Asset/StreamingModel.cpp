/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:05:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "StreamingModel.hpp"
#include "StreamingManager.hpp"

#include <Caramel/Core/JobSystem.hpp>

#include <Jolt/Core/StreamWrapper.h>

#include <glm/gtc/matrix_transform.hpp>

#include <sstream>

namespace
{
    glm::mat4 LocalTransform(const ModelNode& node)
    {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), node.translation);
        glm::mat4 r = glm::mat4_cast(node.rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), node.scale);
        return t * r * s;
    }

    glm::mat4 WorldTransform(const CPUModel& model, int32 nodeIndex)
    {
        if (nodeIndex < 0)
            return glm::mat4(1.0f);
        const ModelNode& node = model.GetNodes()[nodeIndex];
        return WorldTransform(model, node.parentIndex) * LocalTransform(node);
    }

    glm::mat4 FindMeshWorldTransform(const CPUModel& model, uint32 meshIndex)
    {
        for (uint32 nodeIndex = 0; nodeIndex < model.GetNodes().Size(); ++nodeIndex)
        {
            const ModelNode& node = model.GetNodes()[nodeIndex];
            for (int32 referencedMesh : node.meshIndices)
            {
                if (referencedMesh == (int32)meshIndex)
                    return WorldTransform(model, (int32)nodeIndex);
            }
        }
        return glm::mat4(1.0f);
    }
}

void StreamingModel::BeginLoad(TShared<CPUModel> source, uint32 meshIndex, StreamingManager& manager, uint32 requestId)
{
    m_Source = std::move(source);
    m_MeshIndex = meshIndex;
    m_RequestId = requestId;
    m_Gpu = GPUModel(GetMesh());
    m_WorldTransform = FindMeshWorldTransform(*m_Source, m_MeshIndex);

    const ModelMesh& mesh = GetMesh();
    TArray<uint8> vertexBytes(mesh.vertexBufferLength);
    m_Source->LoadVertexBuffer(m_MeshIndex, vertexBytes.Data());
    manager.GetUploadQueue().EnqueueBufferUpload(m_Gpu.GetVertexBuffer(), 0, vertexBytes.Data(), vertexBytes.Size());
    manager.MarkDirty();

    if (mesh.colliderLength > 0)
    {
        JobSystem::Get().RunDetached([this]() {
            TArray<uint8> colliderBytes(GetMesh().colliderLength);
            m_Source->LoadCollider(m_MeshIndex, colliderBytes.Data());

            std::istringstream in(std::string(reinterpret_cast<const char*>(colliderBytes.Data()), colliderBytes.Size()), std::ios::binary);
            JPH::StreamInWrapper streamIn(in);

            JPH::Shape::ShapeResult result = JPH::Shape::sRestoreFromBinaryState(streamIn);
            if (!result.HasError())
            {
                m_ColliderShape = result.Get();
                m_ColliderReady.store(true, std::memory_order_release);
            }
        });
    }

    m_NextLodToLoad = 0;
    RequestNextLOD(manager);
}

bool StreamingModel::RequestNextLOD(StreamingManager& manager)
{
    uint32 residentLOD = m_HighestResidentLOD.load(std::memory_order_acquire);
    if (residentLOD != kNoResidentLOD && residentLOD == CaramelAsset::kLodCount - 1)
        return false;

    uint32 lodIndex = m_NextLodToLoad;
    if (lodIndex == kNoResidentLOD)
        return false;

    if (m_UploadInFlight.exchange(true, std::memory_order_acq_rel))
        return false;

    m_PendingLOD.store(lodIndex, std::memory_order_relaxed);
    m_NextLodToLoad = (lodIndex == CaramelAsset::kLodCount - 1) ? kNoResidentLOD : (lodIndex + 1);

    JobSystem::Get().RunDetached([this, &manager, lodIndex]() {
        const ModelLOD& lod = GetMesh().lods[lodIndex];

        TArray<uint8> lodBytes(lod.GetByteLength());
        m_Source->LoadLOD(m_MeshIndex, lodIndex, lodBytes.Data());

        const uint8* meshletBytes = lodBytes.Data();
        const uint8* meshletVertexBytes = meshletBytes + (lod.meshletVertexBufferOffset - lod.meshletBufferOffset);
        const uint8* meshletTriangleBytes = meshletBytes + (lod.meshletTriangleBufferOffset - lod.meshletBufferOffset);
        const uint8* flatIndexBytes = meshletBytes + (lod.flatIndexBufferOffset - lod.meshletBufferOffset);

        UploadQueue& uploadQueue = manager.GetUploadQueue();
        uploadQueue.EnqueueBufferUpload(m_Gpu.GetMeshletBuffer(lodIndex), 0, meshletBytes, lod.meshletBufferLength);
        uploadQueue.EnqueueBufferUpload(m_Gpu.GetMeshletVertexBuffer(lodIndex), 0, meshletVertexBytes, lod.meshletVertexBufferLength);
        uploadQueue.EnqueueBufferUpload(m_Gpu.GetFlatIndexBuffer(lodIndex), 0, flatIndexBytes, lod.flatIndexBufferLength);
        uint64 fenceValue = uploadQueue.EnqueueBufferUpload(m_Gpu.GetMeshletTriangleBuffer(lodIndex), 0, meshletTriangleBytes, lod.meshletTriangleBufferLength);

        m_PendingFenceValue.store(fenceValue, std::memory_order_release);
        manager.MarkDirty();
    });

    return true;
}

uint64 StreamingModel::GetNextUploadBytes() const
{
    if (HasPendingLOD() || m_NextLodToLoad == kNoResidentLOD)
        return 0;
    return GetMesh().lods[m_NextLodToLoad].GetByteLength();
}

uint64 StreamingModel::PollCompletion(uint64 completedFenceValue)
{
    uint64 pendingFence = m_PendingFenceValue.load(std::memory_order_acquire);
    if (pendingFence == 0 || completedFenceValue < pendingFence)
        return 0;

    uint32 lodIndex = m_PendingLOD.load(std::memory_order_relaxed);
    uint64 retiredBytes = GetMesh().lods[lodIndex].GetByteLength();
    OnLODResident(lodIndex);

    m_PendingFenceValue.store(0, std::memory_order_release);
    m_PendingLOD.store(kNoResidentLOD, std::memory_order_relaxed);
    m_UploadInFlight.store(false, std::memory_order_release);

    return retiredBytes;
}

void StreamingModel::OnLODResident(uint32 lodIndex)
{
    m_HighestResidentLOD.store(lodIndex, std::memory_order_release);
}

const ModelMaterial& StreamingModel::GetMaterial() const
{
    static const ModelMaterial kNoMaterial;

    int32 materialIndex = GetMesh().materialIndex;
    const TArray<ModelMaterial>& materials = m_Source->GetMaterials();
    if (materialIndex < 0 || (uint32)materialIndex >= materials.Size())
        return kNoMaterial;

    return materials[materialIndex];
}
