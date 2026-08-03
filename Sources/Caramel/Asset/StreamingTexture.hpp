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

// Coarse-to-fine progressive streaming for a single texture: creates the full-mip-count GPU
// texture up front, then streams mips in one at a time starting from the coarsest (largest mip
// index) down to the finest (mip 0), via StreamingManager's shared UploadQueue. At most one mip
// is ever in flight at a time -- call RequestNextMip() to kick off the next one once the previous
// has landed (SnapshotResidentMip() != kNoResidentMip).
class StreamingTexture
{
public:
    static constexpr uint32 kNoResidentMip = UINT32_MAX;

    TShared<GPUTexture> BeginLoad(CPUTexture source, StreamingManager& manager);

    // Schedules the next (finer) mip if none is currently in flight and mips remain. Returns
    // false if there was nothing to do (a mip is already pending, or mip 0 is already resident).
    bool RequestNextMip(StreamingManager& manager);

    // Called once per frame by StreamingManager::Update() with the upload queue's fence's
    // completed value; promotes the pending mip to resident once its upload has landed.
    void PollCompletion(uint64 completedFenceValue);

    uint32 GetMipCount() const { return m_Source.GetMipCount(); }
    uint32 GetWidth() const { return m_Source.GetWidth(); }
    uint32 GetHeight() const { return m_Source.GetHeight(); }

    uint32 SnapshotResidentMip() const { return m_HighestResidentMip.load(std::memory_order_acquire); }
    bool HasPendingMip() const { return m_PendingFenceValue.load(std::memory_order_acquire) != 0; }

    // Bindless texture ID scoped to just the currently-resident mip, ready for ImGui::Image --
    // ImTextureID_Invalid while nothing has landed yet.
    ImTextureID GetDisplayTexID() const { return m_DisplayTexID; }

private:
    void OnMipResident(uint32 mipIndex);

    CPUTexture m_Source;
    TShared<GPUTexture> m_Destination;

    std::atomic<uint32> m_HighestResidentMip{ kNoResidentMip };

    // Cursor walking the mip table backwards (coarsest to finest). kNoResidentMip once mip 0 has
    // been requested.
    uint32 m_NextMipToLoad = 0;

    std::atomic<uint64> m_PendingFenceValue{ 0 }; // 0 == nothing in flight
    std::atomic<uint32> m_PendingMip{ kNoResidentMip };

    // One view per mip level, each covering [level, mipCount), all created up front. Creating them
    // lazily and dropping the previous one on every promotion would recycle the bindless slot
    // while frames referencing it are still in flight -- harmless on D3D12, but on Metal the
    // texture view pool entry is rewritten under the GPU and the texture flickers for a frame.
    TArray<agfx::TextureView> m_MipViews;
    ImTextureID m_DisplayTexID = ImTextureID_Invalid;
};
