/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "RenderGraphAllocator.hpp"

#include <algorithm>
#include <utility>

namespace
{
    uint64 AlignUp(uint64 value, uint64 alignment)
    {
        if (alignment == 0)
            return value;
        return (value + alignment - 1) / alignment * alignment;
    }
}

RenderGraphAllocator::RenderGraphAllocator(agfx::Device& device)
    : m_Device(&device)
{
    // A representative 1x1 render-attachment-capable texture -- matches the shape of a real
    // transient color target closely enough to be a reliable proxy for whether this device supports
    // placement heaps at all. {0, 0} back is AGFX's documented "unsupported" signal (see
    // agfxDeviceGetTextureAllocationInfo), not a null-check on CreateHeap.
    agfx::TextureCreateInfo probe{};
    probe.SetFormat(agfx::TextureFormat::RGBA8Unorm)
         .SetUsage(agfx::TextureUsage::Sampled | agfx::TextureUsage::ColorAttachment)
         .SetSize(1, 1);
    agfxAllocationInfo info = m_Device->GetTextureAllocationInfo(probe);
    m_SupportsPlacementHeaps = info.size != 0;
}

void RenderGraphAllocator::BeginFrame(uint32 frameSlot)
{
    m_RetireSlot = frameSlot % FRAMES_IN_FLIGHT;

    // Views first, then the resources they were created against -- the reverse of creation order.
    RetiredResources& bucket = m_Retired[m_RetireSlot];
    bucket.textureViews.Clear();
    bucket.renderTargets.Clear();
    bucket.textures.Clear();
    bucket.buffers.Clear();
}

void RenderGraphAllocator::Retire(agfx::Texture&& texture)
{
    if (texture)
        m_Retired[m_RetireSlot].textures.PushBack(std::move(texture));
}

void RenderGraphAllocator::Retire(agfx::Buffer&& buffer)
{
    if (buffer)
        m_Retired[m_RetireSlot].buffers.PushBack(std::move(buffer));
}

void RenderGraphAllocator::Retire(agfx::TextureView&& view)
{
    if (view)
        m_Retired[m_RetireSlot].textureViews.PushBack(std::move(view));
}

void RenderGraphAllocator::Retire(agfx::RenderTarget&& renderTarget)
{
    if (renderTarget)
        m_Retired[m_RetireSlot].renderTargets.PushBack(std::move(renderTarget));
}

int32 RenderGraphAllocator::FindReusableRegion(const TArray<HeapRegion>& regions, const Request& req) const
{
    for (uint32 r = 0; r < regions.Size(); ++r) {
        const HeapRegion& region = regions[r];
        if (region.freeAtPass > req.firstUsePass)
            continue;
        if (region.size < req.size)
            continue;
        if (AlignUp(region.offset, req.alignment) != region.offset)
            continue;
        return (int32)r;
    }
    return -1;
}

void RenderGraphAllocator::AllocateCommitted(TArray<RGTextureDesc>& textures, TArray<RGBufferDesc>& buffers)
{
    bool createdAny = false;

    for (RGTextureDesc& tex : textures) {
        if (tex.isImported || tex.firstUsePass == UINT32_MAX || tex.ownedTexture)
            continue;
        tex.ownedTexture = m_Device->CreateTexture(tex.createInfo);
        tex.ownedTexture.SetName(tex.name.CStr());
        tex.allocationInfo = m_Device->GetTextureAllocationInfo(tex.createInfo);
        createdAny = true;
    }
    for (RGBufferDesc& buf : buffers) {
        if (buf.isImported || buf.firstUsePass == UINT32_MAX || buf.ownedBuffer)
            continue;
        buf.ownedBuffer = m_Device->CreateBuffer(buf.createInfo);
        buf.ownedBuffer.SetName(buf.name.CStr());
        buf.allocationInfo = m_Device->GetBufferAllocationInfo(buf.createInfo);
        createdAny = true;
    }

    if (createdAny)
        m_Device->MakeResourcesResident();
}

bool RenderGraphAllocator::EnsureHeapCapacity(uint64 requiredSize)
{
    if (requiredSize <= m_HeapCapacity)
        return true;

    agfx::HeapCreateInfo info{};
    info.SetSize(requiredSize);
    agfx::Heap heap = m_Device->CreateHeap(info);
    if (!heap)
        return false;

    m_Heap = std::move(heap);
    m_HeapCapacity = requiredSize;
    return true;
}

void RenderGraphAllocator::Allocate(TArray<RGTextureDesc>& textures, TArray<RGBufferDesc>& buffers)
{
    if (!m_SupportsPlacementHeaps) {
        AllocateCommitted(textures, buffers);
        return;
    }

    TArray<Request> requests;
    for (uint32 i = 0; i < textures.Size(); ++i) {
        RGTextureDesc& tex = textures[i];
        if (tex.isImported || tex.firstUsePass == UINT32_MAX)
            continue;
        tex.allocationInfo = m_Device->GetTextureAllocationInfo(tex.createInfo);
        requests.PushBack({ RGResourceType::Texture, i, tex.allocationInfo.size, tex.allocationInfo.alignment, tex.firstUsePass, tex.lastUsePass });
    }
    for (uint32 i = 0; i < buffers.Size(); ++i) {
        RGBufferDesc& buf = buffers[i];
        if (buf.isImported || buf.firstUsePass == UINT32_MAX)
            continue;
        buf.allocationInfo = m_Device->GetBufferAllocationInfo(buf.createInfo);
        requests.PushBack({ RGResourceType::Buffer, i, buf.allocationInfo.size, buf.allocationInfo.alignment, buf.firstUsePass, buf.lastUsePass });
    }

    if (requests.IsEmpty())
        return;

    std::sort(requests.Begin(), requests.End(), [](const Request& a, const Request& b) {
        if (a.firstUsePass != b.firstUsePass)
            return a.firstUsePass < b.firstUsePass;
        return a.size > b.size;
    });

    TArray<HeapRegion> regions;
    uint64 highWaterMark = 0;
    int32 nextAliasGroup = 0;

    for (const Request& req : requests) {
        int32 chosenRegion = FindReusableRegion(regions, req);

        uint64 offset;
        int32 aliasGroup;
        RGAliasSource aliasSource{};

        if (chosenRegion >= 0) {
            HeapRegion& region = regions[(uint32)chosenRegion];
            offset = region.offset;
            aliasGroup = region.aliasGroup;
            aliasSource.type = region.lastOccupantType;
            aliasSource.index = region.lastOccupantIndex;
            region.freeAtPass = req.lastUsePass + 1;
            region.lastOccupantType = req.type;
            region.lastOccupantIndex = (int32)req.index;
        } else {
            HeapRegion region{};
            region.offset = AlignUp(highWaterMark, req.alignment);
            region.size = req.size;
            region.freeAtPass = req.lastUsePass + 1;
            region.aliasGroup = nextAliasGroup++;
            region.lastOccupantType = req.type;
            region.lastOccupantIndex = (int32)req.index;
            offset = region.offset;
            aliasGroup = region.aliasGroup;
            highWaterMark = region.offset + region.size;
            regions.PushBack(region);
        }

        if (req.type == RGResourceType::Texture) {
            RGTextureDesc& tex = textures[req.index];
            tex.heapAliasGroup = aliasGroup;
            tex.aliasSource = aliasSource;
            tex.pendingHeapOffset = offset;
        } else {
            RGBufferDesc& buf = buffers[req.index];
            buf.heapAliasGroup = aliasGroup;
            buf.aliasSource = aliasSource;
            buf.pendingHeapOffset = offset;
        }
    }

    if (!EnsureHeapCapacity(highWaterMark)) {
        // Heap growth failed (e.g. out of device memory) -- fall back to committed allocation for
        // every resource in this batch rather than creating resources against an undersized heap.
        AllocateCommitted(textures, buffers);
        return;
    }

    for (const Request& req : requests) {
        if (req.type == RGResourceType::Texture) {
            RGTextureDesc& tex = textures[req.index];
            agfx::TextureCreateInfo info = tex.createInfo;
            info.SetHeap(m_Heap, tex.pendingHeapOffset);
            tex.ownedTexture = m_Device->CreateTexture(info);
            tex.ownedTexture.SetName(tex.name.CStr());
        } else {
            RGBufferDesc& buf = buffers[req.index];
            agfx::BufferCreateInfo info = buf.createInfo;
            info.SetHeap(m_Heap, buf.pendingHeapOffset);
            buf.ownedBuffer = m_Device->CreateBuffer(info);
            buf.ownedBuffer.SetName(buf.name.CStr());
        }
    }

    m_Device->MakeResourcesResident();
}
