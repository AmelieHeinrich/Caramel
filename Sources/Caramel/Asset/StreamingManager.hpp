/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Asset/StreamingTexture.hpp>
#include <Caramel/Asset/StreamingModel.hpp>
#include <Caramel/Renderer/UploadQueue.hpp>

#include <atomic>
#include <mutex>

// Drives progressive asset streaming: owns the shared UploadQueue, and each frame flushes any
// upload work recorded since the last tick and polls every tracked streamable for completion.
//
// Textures stream mips coarse-to-fine via StreamingTexture; models stream mesh LODs coarse-to-fine
// via StreamingModel -- both carry the same pending-fence/PollCompletion contract and are polled
// here the same way.
class StreamingManager
{
public:
    void Init(agfx::Device& device);
    void Update();

    TShared<StreamingTexture> LoadTexture(const String& path);
    void LoadDirectory(const String& directory);

    // Loads `path`'s .cmdl and begins streaming every one of its meshes (one StreamingModel per
    // ModelMesh, all sharing the parsed CPUModel). Also kicks off streaming of every material's
    // base color texture via LoadTexture, keyed by materialIndex so the render pass can look each
    // mesh's texture up via ModelMesh::materialIndex.
    void LoadModel(const String& path);

    UploadQueue& GetUploadQueue() { return m_UploadQueue; }
    void MarkDirty() { m_Dirty.store(true, std::memory_order_relaxed); }

    const TArray<TShared<StreamingTexture>>& GetTextures() const { return m_Textures; }
    const TArray<TShared<StreamingModel>>& GetModels() const { return m_Models; }
    TShared<StreamingTexture> GetMaterialTexture(int32 materialIndex) const;

private:
    UploadQueue m_UploadQueue;
    std::atomic<bool> m_Dirty{ false };

    std::mutex m_TexturesMutex;
    TArray<TShared<StreamingTexture>> m_Textures;

    std::mutex m_ModelsMutex;
    TArray<TShared<StreamingModel>> m_Models;
    TDictionary<int32, TShared<StreamingTexture>> m_MaterialTextures;
};
