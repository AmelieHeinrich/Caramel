/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:17:12
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <AGFX/agfx.hpp>

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Common.hpp>

#include <mutex>

class UploadQueue
{
public:
    void Init(agfx::Device& device);

    uint64 EnqueueTextureUpload(agfx::Texture& texture, uint32 mipLevel, const void* data, size_t dataSize, uint32 width, uint32 height, uint32 bytesPerRow);
    uint64 Flush();

    agfx::Fence& GetFence() { return m_Fence; }

private:
    static constexpr uint64 kStagingBufferSize = 64 * 1024 * 1024;

    // Submits the current frame's command buffer and advances to the next ring slot. Assumes
    // m_RecordMutex is already held by the caller.
    uint64 FlushLocked();

    agfx::Device* m_Device = nullptr;
    agfx::CommandQueue m_CopyQueue;
    agfx::Fence m_Fence;
    uint64 m_NextFenceValue = 1;

    struct UploadFrame
    {
        agfx::Buffer stagingBuffer;
        agfx::CommandBuffer commandBuffer;
        uint64 writeOffset = 0;
        uint64 fenceValue = 0;
        // Whether Begin() has been called without a matching End(). A command allocator may only
        // have one open command buffer, so the buffer is only opened once work is actually
        // recorded into the slot.
        bool recording = false;
    };
    UploadFrame m_Frames[FRAMES_IN_FLIGHT];
    uint32 m_CurrentFrameIndex = 0;

    std::mutex m_RecordMutex;
};
