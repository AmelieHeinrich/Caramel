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

    const uint32 mipCount = m_Source.GetMipCount();
    m_MipViews.reserve(mipCount);
    for (uint32 mip = 0; mip < mipCount; ++mip)
    {
        agfx::TextureViewCreateInfo viewInfo = agfx::TextureViewCreateInfo().SetTexture(m_Destination->GetTexture())
                                                                            .SetFormat(CaramelAsset::ToAgfxFormat(m_Source.GetFormat()))
                                                                            .SetMipRange(mip, mipCount - mip)
                                                                            .SetWriteable(false);
        m_MipViews.push_back(Renderer::Get().GetDevice().CreateTextureView(viewInfo));
    }

    m_NextMipToLoad = m_Source.GetMipCount() - 1;
    RequestNextMip(manager);

    return m_Destination;
}

bool StreamingTexture::RequestNextMip(StreamingManager& manager)
{
    uint32 residentMip = m_HighestResidentMip.load(std::memory_order_acquire);
    if (residentMip != kNoResidentMip && residentMip == 0)
        return false;

    uint32 mipIndex = m_NextMipToLoad;
    if (mipIndex == kNoResidentMip)
        return false;

    if (m_UploadInFlight.exchange(true, std::memory_order_acq_rel))
        return false;

    m_PendingMip.store(mipIndex, std::memory_order_relaxed);
    m_NextMipToLoad = (mipIndex == 0) ? kNoResidentMip : (mipIndex - 1);

    JobSystem::Get().RunDetached([this, &manager, mipIndex]() {
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

uint64 StreamingTexture::GetNextUploadBytes() const
{
    if (HasPendingMip() || m_NextMipToLoad == kNoResidentMip)
        return 0;
    return m_Source.GetMip(m_NextMipToLoad).size;
}

uint64 StreamingTexture::PollCompletion(uint64 completedFenceValue)
{
    uint64 pendingFence = m_PendingFenceValue.load(std::memory_order_acquire);
    if (pendingFence == 0 || completedFenceValue < pendingFence)
        return 0;

    uint32 mipIndex = m_PendingMip.load(std::memory_order_relaxed);
    uint64 retiredBytes = m_Source.GetMip(mipIndex).size;
    OnMipResident(mipIndex);

    m_PendingFenceValue.store(0, std::memory_order_release);
    m_PendingMip.store(kNoResidentMip, std::memory_order_relaxed);
    m_UploadInFlight.store(false, std::memory_order_release);

    return retiredBytes;
}

void StreamingTexture::OnMipResident(uint32 mipIndex)
{
    Renderer::Get().EnqueueMipTransition(m_Destination->GetTexture(), mipIndex);

    m_DisplayTexID = (ImTextureID)(intptr_t)m_MipViews[mipIndex].GetHandle();

    m_HighestResidentMip.store(mipIndex, std::memory_order_release);
}
