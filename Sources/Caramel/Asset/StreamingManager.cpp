/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:30:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "StreamingManager.hpp"

#include <Caramel/Core/JobSystem.hpp>
#include <Caramel/Core/Logger.hpp>

#include <algorithm>
#include <filesystem>

void StreamingManager::Init(agfx::Device& device)
{
    m_Device = &device;
    m_UploadQueue.Init(device);
}

void StreamingManager::ProcessPendingInits()
{
    TArray<PendingTextureInit> textureBatch;
    TArray<PendingModelInit*> modelBatch;
    {
        std::lock_guard lock(m_PendingMutex);

        uint32 textureCount = std::min<uint32>(kTextureInitsPerUpdate, (uint32)m_PendingTextureInits.Size());
        for (uint32 i = 0; i < textureCount; ++i)
            textureBatch.PushBack(std::move(m_PendingTextureInits[i]));
        m_PendingTextureInits.erase(m_PendingTextureInits.begin(), m_PendingTextureInits.begin() + textureCount);
    }

    bool createdAnything = !textureBatch.IsEmpty();

    for (PendingTextureInit& pending : textureBatch)
    {
        TShared<StreamingTexture> texture = MakeShared<StreamingTexture>();
        texture->BeginLoad(std::move(pending.source), *this);

        m_Textures.PushBack(texture);
        if (pending.materialIndex >= 0)
            m_MaterialTextures[MaterialKey(pending.requestId, pending.materialIndex, pending.slot)] = texture;
    }

    uint32 meshBudget = kMeshInitsPerUpdate;
    while (meshBudget > 0)
    {
        TShared<CPUModel> model;
        uint32 meshIndex = 0;
        uint32 requestId = 0;
        {
            std::lock_guard lock(m_PendingMutex);
            if (m_PendingModelInits.IsEmpty())
                break;

            PendingModelInit& pending = m_PendingModelInits[0];
            model = pending.model;
            meshIndex = pending.nextMeshIndex++;
            requestId = pending.requestId;
            if (pending.nextMeshIndex >= model->GetMeshes().Size())
                m_PendingModelInits.erase(m_PendingModelInits.begin());
        }

        TShared<StreamingModel> streamingModel = MakeShared<StreamingModel>();
        streamingModel->BeginLoad(model, meshIndex, *this, requestId);
        m_Models.PushBack(std::move(streamingModel));

        createdAnything = true;
        --meshBudget;
    }

    if (createdAnything)
        m_Device->MakeResourcesResident();
}

void StreamingManager::Update()
{
    JobSystem::Get().CollectGarbage();

    ProcessPendingInits();

    if (m_Dirty.exchange(false, std::memory_order_acq_rel))
        m_UploadQueue.Flush();

    uint64 completed = m_UploadQueue.GetFence().GetCompletedValue();

    uint64 retired = 0;
    for (TShared<StreamingTexture>& texture : m_Textures)
        retired += texture->PollCompletion(completed);

    for (TShared<StreamingModel>& model : m_Models)
        retired += model->PollCompletion(completed);

    m_BytesInFlight -= std::min(retired, m_BytesInFlight);

    if (m_AutoStream)
        PumpStreaming();
}

void StreamingManager::PumpStreaming()
{
    for (TShared<StreamingModel>& model : m_Models)
    {
        uint64 bytes = model->GetNextUploadBytes();
        if (bytes == 0 || m_BytesInFlight + bytes > kMaxBytesInFlight)
            continue;

        if (model->RequestNextLOD(*this))
            m_BytesInFlight += bytes;
    }

    for (TShared<StreamingTexture>& texture : m_Textures)
    {
        uint64 bytes = texture->GetNextUploadBytes();
        if (bytes == 0 || m_BytesInFlight + bytes > kMaxBytesInFlight)
            continue;

        if (texture->RequestNextMip(*this))
            m_BytesInFlight += bytes;
    }
}

void StreamingManager::LoadTexture(const String& path)
{
    JobSystem::Get().RunDetached([this, path]() { StagePendingTexture(path, -1, 0); });
}

void StreamingManager::StagePendingTexture(const String& path, int32 materialIndex, uint32 requestId, MaterialTextureSlot slot)
{
    CPUTexture source(path);
    if (!source.IsValid())
    {
        CARAMEL_ERROR("StreamingManager: failed to load texture '{}'", path.CStr());
        return;
    }

    std::lock_guard lock(m_PendingMutex);
    m_PendingTextureInits.PushBack(PendingTextureInit{ std::move(source), materialIndex, requestId, slot });
}

uint32 StreamingManager::LoadModel(const String& path)
{
    uint32 requestId = m_NextRequestId++;
    JobSystem::Get().RunDetached([this, path, requestId]() { ExecuteModelLoad(path, requestId); });
    return requestId;
}

void StreamingManager::ExecuteModelLoad(const String& path, uint32 requestId)
{
    TShared<CPUModel> model = MakeShared<CPUModel>(path);
    if (!model->IsValid())
    {
        CARAMEL_ERROR("StreamingManager: failed to load model '{}'", path.CStr());
        return;
    }

    for (int32 materialIndex = 0; materialIndex < (int32)model->GetMaterials().Size(); ++materialIndex)
    {
        const ModelMaterial& material = model->GetMaterials()[materialIndex];

        struct { const String& path; MaterialTextureSlot slot; } slots[] = {
            { material.baseColorTexture, MaterialTextureSlot::BaseColor },
            { material.normalTexture, MaterialTextureSlot::Normal },
            { material.metallicRoughnessTexture, MaterialTextureSlot::MetallicRoughness },
            { material.occlusionTexture, MaterialTextureSlot::Occlusion },
            { material.emissiveTexture, MaterialTextureSlot::Emissive },
        };
        for (const auto& s : slots)
        {
            if (!s.path.Empty())
                StagePendingTexture(s.path, materialIndex, requestId, s.slot);
        }
    }

    if (model->GetMeshes().IsEmpty())
        return;

    std::lock_guard lock(m_PendingMutex);
    m_PendingModelInits.PushBack(PendingModelInit{ std::move(model), 0, requestId });
}

TShared<StreamingTexture> StreamingManager::GetMaterialTexture(uint32 requestId, int32 materialIndex, MaterialTextureSlot slot) const
{
    auto it = m_MaterialTextures.find(MaterialKey(requestId, materialIndex, slot));
    return it != m_MaterialTextures.end() ? it->second : nullptr;
}

void StreamingManager::LoadDirectory(const String& directory)
{
    JobSystem::Get().RunDetached([this, directory]() { ExecuteDirectoryLoad(directory); });
}

void StreamingManager::ExecuteDirectoryLoad(const String& directory)
{
    namespace fs = std::filesystem;

    if (!fs::is_directory(directory.CStr()))
    {
        CARAMEL_ERROR("StreamingManager: '{}' is not a directory", directory.CStr());
        return;
    }

    for (const auto& entry : fs::directory_iterator(directory.CStr()))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".ctex")
            continue;
        StagePendingTexture(entry.path().string(), -1, 0);
    }
}
