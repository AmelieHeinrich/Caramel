/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:30:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "StreamingManager.hpp"

#include <Caramel/Core/Logger.hpp>

#include <filesystem>

void StreamingManager::Init(agfx::Device& device)
{
    m_UploadQueue.Init(device);
}

void StreamingManager::Update()
{
    if (m_Dirty.exchange(false, std::memory_order_acq_rel))
        m_UploadQueue.Flush();

    uint64 completed = m_UploadQueue.GetFence().GetCompletedValue();

    {
        std::lock_guard lock(m_TexturesMutex);
        for (TShared<StreamingTexture>& texture : m_Textures)
            texture->PollCompletion(completed);
    }

    {
        std::lock_guard lock(m_ModelsMutex);
        for (TShared<StreamingModel>& model : m_Models)
            model->PollCompletion(completed);
    }
}

TShared<StreamingTexture> StreamingManager::LoadTexture(const String& path)
{
    CPUTexture source(path);
    if (!source.IsValid())
    {
        CARAMEL_ERROR("StreamingManager: failed to load texture '{}'", path.CStr());
        return nullptr;
    }

    TShared<StreamingTexture> texture = MakeShared<StreamingTexture>();
    texture->BeginLoad(std::move(source), *this);

    std::lock_guard lock(m_TexturesMutex);
    m_Textures.PushBack(texture);
    return texture;
}

void StreamingManager::LoadModel(const String& path)
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
        if (material.baseColorTexture.Empty())
            continue;

        TShared<StreamingTexture> texture = LoadTexture(material.baseColorTexture);
        if (texture)
            m_MaterialTextures[materialIndex] = texture;
    }

    std::lock_guard lock(m_ModelsMutex);
    for (uint32 meshIndex = 0; meshIndex < model->GetMeshes().Size(); ++meshIndex)
    {
        TShared<StreamingModel> streamingModel = MakeShared<StreamingModel>();
        streamingModel->BeginLoad(model, meshIndex, *this);
        m_Models.PushBack(streamingModel);
    }
}

TShared<StreamingTexture> StreamingManager::GetMaterialTexture(int32 materialIndex) const
{
    auto it = m_MaterialTextures.find(materialIndex);
    return it != m_MaterialTextures.end() ? it->second : nullptr;
}

void StreamingManager::LoadDirectory(const String& directory)
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
        LoadTexture(entry.path().string());
    }
}
