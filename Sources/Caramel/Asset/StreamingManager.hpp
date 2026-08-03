/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Asset/StreamingTexture.hpp>
#include <Caramel/Renderer/UploadQueue.hpp>

#include <atomic>
#include <mutex>

// Drives progressive asset streaming: owns the shared UploadQueue, and each frame flushes any
// upload work recorded since the last tick and polls every tracked streamable for completion.
//
// Texture streaming is the only asset kind wired up today, but the shape is generic on purpose --
// a future mesh LOD streamer would carry the same pending-fence/PollCompletion contract as
// StreamingTexture and just needs its own tracked list + poll loop added to Update(), no rework
// of this class.
class StreamingManager
{
public:
    void Init(agfx::Device& device);
    void Update();

    TShared<StreamingTexture> LoadTexture(const String& path);
    void LoadDirectory(const String& directory);

    UploadQueue& GetUploadQueue() { return m_UploadQueue; }
    void MarkDirty() { m_Dirty.store(true, std::memory_order_relaxed); }

    const TArray<TShared<StreamingTexture>>& GetTextures() const { return m_Textures; }

private:
    UploadQueue m_UploadQueue;
    std::atomic<bool> m_Dirty{ false };

    std::mutex m_TexturesMutex;
    TArray<TShared<StreamingTexture>> m_Textures;
};
