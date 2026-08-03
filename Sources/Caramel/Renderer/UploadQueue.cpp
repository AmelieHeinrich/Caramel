/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:20:18
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "UploadQueue.hpp"

void UploadQueue::Init(agfx::Device& device)
{
    m_Device = &device;
    m_CopyQueue = device.CreateCommandQueue(agfx::CommandQueueType::Transfer);
    m_Fence = device.CreateFence();

    for (UploadFrame& frame : m_Frames) {
        agfx::BufferCreateInfo stagingInfo;
        stagingInfo.SetSize(kStagingBufferSize).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        frame.stagingBuffer = device.CreateBuffer(stagingInfo);
        frame.commandBuffer = device.CreateCommandBuffer(m_CopyQueue);
    }
}

uint64 UploadQueue::AllocateStagingLocked(uint64 size, uint64 alignment)
{
    uint64 offset = (m_Frames[m_CurrentFrameIndex].writeOffset + alignment - 1) & ~(alignment - 1);
    if (offset + size > kStagingBufferSize)
    {
        FlushLocked();
        offset = 0;
    }

    UploadFrame& frame = m_Frames[m_CurrentFrameIndex];
    if (!frame.recording)
    {
        frame.commandBuffer.Begin();
        frame.recording = true;
    }

    frame.writeOffset = offset + size;
    return offset;
}

uint64 UploadQueue::EnqueueTextureUpload(agfx::Texture& texture, uint32 mipLevel, const void* data, size_t dataSize, uint32 width, uint32 height, uint32 bytesPerRow)
{
    assert(dataSize <= kStagingBufferSize && "Upload larger than a single staging buffer");

    std::lock_guard lock(m_RecordMutex);

    uint64 srcOffset = AllocateStagingLocked(dataSize, kTextureCopyAlignment);
    UploadFrame& frame = m_Frames[m_CurrentFrameIndex];

    {
        agfx::MappedBuffer mapped(frame.stagingBuffer);
        std::memcpy(mapped.As<uint8_t>() + srcOffset, data, dataSize);
    }

    agfx::TextureRegion region;
    region.SetSize(width, height);

    frame.commandBuffer.TextureBarrier(texture, agfx::ResourceState::Common, agfx::ResourceState::CopyDest, mipLevel, 0);

    auto pass = frame.commandBuffer.BeginComputePass("Upload mip");
    pass.CopyBufferToTexture(frame.stagingBuffer, srcOffset, texture, region, mipLevel, 0, bytesPerRow, dataSize);
    pass.End();

    return m_NextFenceValue;
}

uint64 UploadQueue::EnqueueBufferUpload(agfx::Buffer& dst, uint64 dstOffset, const void* data, size_t dataSize)
{
    assert(dataSize <= kStagingBufferSize && "Upload larger than a single staging buffer");

    std::lock_guard lock(m_RecordMutex);

    uint64 srcOffset = AllocateStagingLocked(dataSize, kBufferCopyAlignment);
    UploadFrame& frame = m_Frames[m_CurrentFrameIndex];

    {
        agfx::MappedBuffer mapped(frame.stagingBuffer);
        std::memcpy(mapped.As<uint8_t>() + srcOffset, data, dataSize);
    }

    auto pass = frame.commandBuffer.BeginComputePass("Upload buffer");
    pass.CopyBufferToBuffer(frame.stagingBuffer, dst, srcOffset, dstOffset, dataSize);
    pass.End();

    return m_NextFenceValue;
}

uint64 UploadQueue::Flush()
{
    std::lock_guard lock(m_RecordMutex);
    return FlushLocked();
}

uint64 UploadQueue::FlushLocked()
{
    UploadFrame& frame = m_Frames[m_CurrentFrameIndex];

    if (!frame.recording)
        return m_NextFenceValue - 1;

    frame.commandBuffer.End();
    frame.recording = false;
    m_CopyQueue.Submit(frame.commandBuffer);

    uint64_t signalValue = m_NextFenceValue++;
    m_CopyQueue.Signal(m_Fence, signalValue);
    frame.fenceValue = signalValue;

    m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % FRAMES_IN_FLIGHT;
    UploadFrame& next = m_Frames[m_CurrentFrameIndex];

    m_Fence.Wait(next.fenceValue);
    next.commandBuffer.Reset();
    next.writeOffset = 0;

    return signalValue;
}
