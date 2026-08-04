/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <AGFX/agfx.hpp>
#include <imgui.h>

class ImGuiRenderer
{
public:
    ImGuiRenderer(agfx::Device& device, agfx::CommandQueue& queue, agfx::TextureFormat colorFormat, uint32 framesInFlight);
    ~ImGuiRenderer();

    void RenderDrawData(ImDrawData* drawData, agfx::RenderPass& renderPass, uint32 fbWidth, uint32 fbHeight, uint32 frameIndex);

private:
    void UpdateTextures(ImDrawData* drawData);
    void UpdateTexture(ImTextureData* tex);
    void DestroyTexture(ImTextureData* tex);
    void UploadTexture(agfx::Texture& dstTexture, const agfxTextureRegion& region, const void* data, uint32 dataSize, uint32 bytesPerRow, uint32 bytesPerImage, agfx::ResourceState oldState);
    void FlushUploads();

    agfx::Device* m_Device;
    agfx::CommandQueue* m_Queue;

    agfx::Sampler m_Sampler;

    TArray<agfx::Buffer> m_VertexBuffers;
    TArray<agfx::BufferView> m_VertexBufferViews;
    TArray<uint64> m_VertexBufferCapacity;
    TArray<agfx::Buffer> m_IndexBuffers;
    TArray<uint64> m_IndexBufferCapacity;

    agfx::CommandBuffer m_UploadCmdBuffer;
    agfx::Fence m_UploadFence;
    uint64 m_UploadFenceValue = 0;
    TArray<agfx::Buffer> m_PendingStagingBuffers;
};
