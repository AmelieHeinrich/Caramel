/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:06:25
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "StreamingTexture.hpp"
#include "StreamingManager.hpp"

#include <Caramel/Renderer/Renderer.hpp>
#include <Caramel/Core/JobSystem.hpp>
#include <CaramelAsset/Format.hpp>

TShared<GPUTexture> StreamingTexture::BeginLoad(CPUTexture source, StreamingManager& manager)
{
    m_Source = std::move(source);

    agfx::TextureCreateInfo textureInfo = agfx::TextureCreateInfo().SetSize(m_Source.GetWidth(), m_Source.GetHeight())
                                                                   .SetFormat(CaramelAsset::ToAgfxFormat(m_Source.GetFormat()))
                                                                   .SetMipLevels(m_Source.GetMipCount())
                                                                   .SetType(agfx::TextureType::Texture2D)
                                                                   .SetUsage(agfx::TextureUsage::Sampled);
    m_Destination = MakeShared<GPUTexture>(textureInfo);

    m_NextMipToLoad = m_Source.GetMipCount() - 1;
    RequestNextMip(manager);

    return m_Destination;
}

bool StreamingTexture::RequestNextMip(StreamingManager& manager)
{
    if (HasPendingMip())
        return false;

    uint32 residentMip = m_HighestResidentMip.load(std::memory_order_acquire);
    if (residentMip != kNoResidentMip && residentMip == 0)
        return false; // Already fully resident.

    uint32 mipIndex = m_NextMipToLoad;
    if (mipIndex == kNoResidentMip)
        return false;

    m_PendingMip.store(mipIndex, std::memory_order_relaxed);
    m_NextMipToLoad = (mipIndex == 0) ? kNoResidentMip : (mipIndex - 1);

    JobSystem::Get().Schedule([this, &manager, mipIndex]() {
        const TextureMip& mip = m_Source.GetMip(mipIndex);
        TArray<uint8> compressed(mip.size);
        m_Source.LoadMip(mipIndex, compressed.data());

        uint32 bytesPerRow = CaramelAsset::GetBytesPerRow(m_Source.GetFormat(), mip.width);
        uint32 copyWidth, copyHeight;
        CaramelAsset::GetBlockAlignedExtent(m_Source.GetFormat(), mip.width, mip.height, copyWidth, copyHeight);
        uint64 fenceValue = manager.GetUploadQueue().EnqueueTextureUpload(m_Destination->GetTexture(), mipIndex, compressed.data(), compressed.size(), copyWidth, copyHeight, bytesPerRow);

        m_PendingFenceValue.store(fenceValue, std::memory_order_release);
        manager.MarkDirty();
    });

    return true;
}

void StreamingTexture::PollCompletion(uint64 completedFenceValue)
{
    uint64 pendingFence = m_PendingFenceValue.load(std::memory_order_acquire);
    if (pendingFence == 0 || completedFenceValue < pendingFence)
        return;

    uint32 mipIndex = m_PendingMip.load(std::memory_order_relaxed);
    OnMipResident(mipIndex);

    m_PendingFenceValue.store(0, std::memory_order_release);
    m_PendingMip.store(kNoResidentMip, std::memory_order_relaxed);
}

void StreamingTexture::OnMipResident(uint32 mipIndex)
{
    agfx::TextureViewCreateInfo viewInfo = agfx::TextureViewCreateInfo().SetTexture(m_Destination->GetTexture())
                                                                        .SetMipRange(mipIndex, 1)
                                                                        .SetWriteable(false);
    m_DisplayView = Renderer::Get().GetDevice().CreateTextureView(viewInfo);
    m_DisplayTexID = (ImTextureID)(intptr_t)m_DisplayView.GetHandle();

    m_HighestResidentMip.store(mipIndex, std::memory_order_release);
}
