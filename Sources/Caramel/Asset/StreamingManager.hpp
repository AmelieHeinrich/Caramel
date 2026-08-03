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

class StreamingManager
{
public:
    void Init(agfx::Device& device);
    void Update();

    void LoadTexture(const String& path);
    void LoadDirectory(const String& directory);
    void LoadModel(const String& path);

    void SetAutoStream(bool value) { m_AutoStream = value; }
    bool GetAutoStream() const { return m_AutoStream; }

    void PumpStreaming();

    uint64 GetBytesInFlight() const { return m_BytesInFlight; }
    static constexpr uint64 GetMaxBytesInFlight() { return kMaxBytesInFlight; }

    UploadQueue& GetUploadQueue() { return m_UploadQueue; }
    void MarkDirty() { m_Dirty.store(true, std::memory_order_relaxed); }

    const TArray<TShared<StreamingTexture>>& GetTextures() const { return m_Textures; }
    const TArray<TShared<StreamingModel>>& GetModels() const { return m_Models; }
    TShared<StreamingTexture> GetMaterialTexture(int32 materialIndex) const;

private:
    struct PendingTextureInit
    {
        CPUTexture source;
        int32 materialIndex = -1;
    };

    struct PendingModelInit
    {
        TShared<CPUModel> model;
        uint32 nextMeshIndex = 0;
    };

    void ExecuteModelLoad(const String& path);
    void ExecuteDirectoryLoad(const String& directory);
    void StagePendingTexture(const String& path, int32 materialIndex);
    void ProcessPendingInits();

    static constexpr uint32 kMeshInitsPerUpdate = 8;
    static constexpr uint32 kTextureInitsPerUpdate = 4;
    static constexpr uint64 kMaxBytesInFlight = UploadQueue::kStagingBufferSize / 2;

    bool m_AutoStream = true;
    uint64 m_BytesInFlight = 0;

    agfx::Device* m_Device = nullptr;
    UploadQueue m_UploadQueue;
    std::atomic<bool> m_Dirty{ false };

    TArray<TShared<StreamingTexture>> m_Textures;
    TArray<TShared<StreamingModel>> m_Models;
    TDictionary<int32, TShared<StreamingTexture>> m_MaterialTextures;

    std::mutex m_PendingMutex;
    TArray<PendingTextureInit> m_PendingTextureInits;
    TArray<PendingModelInit> m_PendingModelInits;
};
