/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <AGFX/agfx.hpp>
#include <imgui.h>

// Hand-written Dear ImGui renderer backend for AGFX -- functionally equivalent to
// Sources/ThirdParty/AGFXImGui/imgui_impl_agfx.cpp (font/texture staging upload, per-frame
// dynamic vertex/index buffers read bindlessly, push-constant-driven draw loop), but built
// against the agfx:: C++ RAII wrapper (matching Renderer.cpp's style) and sourcing its pipeline
// through ShaderServer instead of a precompiled shader module handed in at init -- so editing
// Content/Shaders/ImGui.hlsl and saving hot-reloads the pipeline this renderer draws with.
//
// Does not touch the SDL3 *platform* backend (imgui_impl_sdl3) -- that's initialized/driven
// directly by Application.cpp, since it needs access to each polled SDL_Event.
class ImGuiRenderer
{
public:
    ImGuiRenderer(agfx::Device& device, agfx::CommandQueue& queue, agfx::TextureFormat colorFormat, uint32 framesInFlight);
    ~ImGuiRenderer();

    void RenderDrawData(ImDrawData* drawData, agfx::RenderPass& renderPass, uint32 fbWidth, uint32 fbHeight, uint32 frameIndex);

    // Bold display font (Quicksand) for section headers / titles -- body text uses io.FontDefault.
    ImFont* GetHeaderFont() const { return m_HeaderFont; }

private:
    void UpdateTextures(ImDrawData* drawData);
    void UpdateTexture(ImTextureData* tex);
    void DestroyTexture(ImTextureData* tex);
    void UploadTexture(agfx::Texture& dstTexture, const agfxTextureRegion& region, const void* data, uint32 dataSize, uint32 bytesPerRow, uint32 bytesPerImage);
    void FlushUploads();

    agfx::Device* m_Device;
    agfx::CommandQueue* m_Queue;

    agfx::Sampler m_Sampler;
    ImFont* m_HeaderFont = nullptr;

    TArray<agfx::Buffer> m_VertexBuffers;
    TArray<agfx::BufferView> m_VertexBufferViews;
    TArray<uint64> m_VertexBufferCapacity;
    TArray<agfx::Buffer> m_IndexBuffers;
    TArray<uint64> m_IndexBufferCapacity;

    // Staging-buffer uploader for font/texture creation & updates -- mirrors
    // ImGui_ImplAGFX_Uploader, since D3D12/Vulkan default-heap textures aren't CPU-mappable.
    agfx::CommandBuffer m_UploadCmdBuffer;
    agfx::Fence m_UploadFence;
    uint64 m_UploadFenceValue = 0;
    agfx::ComputePass m_ActiveUploadPass;
    TArray<agfx::Buffer> m_PendingStagingBuffers;
};
