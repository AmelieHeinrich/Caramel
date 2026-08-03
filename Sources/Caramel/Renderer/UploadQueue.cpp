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

uint64 UploadQueue::EnqueueTextureUpload(agfx::Texture& texture, uint32 mipLevel, const void* data, size_t dataSize, uint32 width, uint32 height, uint32 bytesPerRow)
{
    assert(dataSize <= kStagingBufferSize && "Upload larger than a single staging buffer");

    std::lock_guard lock(m_RecordMutex);

    // A batch of independent uploads (e.g. every streaming texture requesting its next mip in
    // the same tick) can outgrow one frame's staging buffer before the caller gets a chance to
    // Flush() -- flush early rather than writing/copying past the buffer.
    if (m_Frames[m_CurrentFrameIndex].writeOffset + dataSize > kStagingBufferSize)
        FlushLocked();

    UploadFrame& frame = m_Frames[m_CurrentFrameIndex];
    if (!frame.recording)
    {
        frame.commandBuffer.Begin();
        frame.recording = true;
    }

    // Copy into this frame's staging buffer at the current write offset.
    {
        agfx::MappedBuffer mapped(frame.stagingBuffer);
        std::memcpy(mapped.As<uint8_t>() + frame.writeOffset, data, dataSize);
    }

    agfx::TextureRegion region;
    region.SetSize(width, height);

    // The copy contract wants the destination mip in CopyDest; until its first transition a fresh
    // texture's mips sit in Common. The matching CopyDest -> PixelShaderResource transition happens
    // on the graphics queue once the upload fence signals (Renderer::EnqueueMipTransition): shader
    // stages are not valid barrier targets on a transfer queue, and the graphics-side barrier
    // doubles as the cross-queue visibility sync for the copied data.
    frame.commandBuffer.TextureBarrier(texture, agfx::ResourceState::Common, agfx::ResourceState::CopyDest, mipLevel, 0);

    auto pass = frame.commandBuffer.BeginComputePass("Upload mip");
    pass.CopyBufferToTexture(frame.stagingBuffer, frame.writeOffset, texture, region, mipLevel, 0, bytesPerRow, dataSize);
    pass.End();

    frame.writeOffset += dataSize;

    return m_NextFenceValue;
}

uint64 UploadQueue::EnqueueBufferUpload(agfx::Buffer& dst, uint64 dstOffset, const void* data, size_t dataSize)
{
    assert(dataSize <= kStagingBufferSize && "Upload larger than a single staging buffer");

    std::lock_guard lock(m_RecordMutex);

    if (m_Frames[m_CurrentFrameIndex].writeOffset + dataSize > kStagingBufferSize)
        FlushLocked();

    UploadFrame& frame = m_Frames[m_CurrentFrameIndex];
    if (!frame.recording)
    {
        frame.commandBuffer.Begin();
        frame.recording = true;
    }

    {
        agfx::MappedBuffer mapped(frame.stagingBuffer);
        std::memcpy(mapped.As<uint8_t>() + frame.writeOffset, data, dataSize);
    }

    auto pass = frame.commandBuffer.BeginComputePass("Upload buffer");
    pass.CopyBufferToBuffer(frame.stagingBuffer, dst, frame.writeOffset, dstOffset, dataSize);
    pass.End();

    frame.writeOffset += dataSize;

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

    // Nothing was recorded into this slot -- there is no command buffer to close and submitting
    // would burn a fence value for no work.
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

    // Before reusing this frame slot, make sure its last submission actually finished. The
    // command buffer is left closed; the next Enqueue reopens it.
    m_Fence.Wait(next.fenceValue);
    next.commandBuffer.Reset();
    next.writeOffset = 0;

    return signalValue;
}
