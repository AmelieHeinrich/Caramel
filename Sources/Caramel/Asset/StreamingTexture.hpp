/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:00:16
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Asset/Texture.hpp>
#include <Caramel/Asset/GPUTexture.hpp>

#include <imgui.h>

#include <atomic>

class StreamingManager;

class StreamingTexture
{
public:
    static constexpr uint32 kNoResidentMip = UINT32_MAX;

    TShared<GPUTexture> BeginLoad(CPUTexture source, StreamingManager& manager);

    bool RequestNextMip(StreamingManager& manager);
    uint64 PollCompletion(uint64 completedFenceValue);
    uint64 GetNextUploadBytes() const;

    uint32 GetMipCount() const { return m_Source.GetMipCount(); }
    uint32 GetWidth() const { return m_Source.GetWidth(); }
    uint32 GetHeight() const { return m_Source.GetHeight(); }

    uint32 SnapshotResidentMip() const { return m_HighestResidentMip.load(std::memory_order_acquire); }
    bool HasPendingMip() const { return m_UploadInFlight.load(std::memory_order_acquire); }

    ImTextureID GetDisplayTexID() const { return m_DisplayTexID; }

private:
    void OnMipResident(uint32 mipIndex);

    CPUTexture m_Source;
    TShared<GPUTexture> m_Destination;

    std::atomic<uint32> m_HighestResidentMip{ kNoResidentMip };
    uint32 m_NextMipToLoad = 0;

    std::atomic<bool> m_UploadInFlight{ false };
    std::atomic<uint64> m_PendingFenceValue{ 0 };
    std::atomic<uint32> m_PendingMip{ kNoResidentMip };

    TArray<agfx::TextureView> m_MipViews;
    ImTextureID m_DisplayTexID = ImTextureID_Invalid;
};
