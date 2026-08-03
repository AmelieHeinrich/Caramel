/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 21:25:29
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/NativeHandle.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/UploadQueue.hpp>
#include <Caramel/Renderer/Camera.hpp>

#include <AGFX/agfx.hpp>
#include <SDL3/SDL.h>

class DebugRenderer;
class ImGuiRenderer;
class SponzaRenderer;
class StreamingManager;

class Renderer
{
public:
    Renderer(SDL_Window* window);
    ~Renderer();

    void Render(const Camera& camera, StreamingManager& streamingManager);
    void Resize();
    void WaitIdle() { m_Fence.Wait(m_FenceValue); }

    /// Queues a CopyDest -> PixelShaderResource transition for one mip, recorded on the graphics
    /// queue at the start of the next frame. Used by texture streaming: the transfer queue leaves
    /// each uploaded mip in CopyDest (shader stages are not valid barrier targets there), so the
    /// graphics queue takes it the rest of the way before anything samples it.
    void EnqueueMipTransition(agfx::Texture& texture, uint32 mip);

    agfx::Device& GetDevice() { return m_Device; }
    agfxDeviceInfo GetDeviceInfo() const { return m_Device.GetInfo(); }

    static Renderer& Get() { return *s_Instance; }

private:
    static Renderer* s_Instance;

    SDL_Window* m_Window;
    TUnique<NativeHandle> m_NativeHandle;
    bool m_ResizeNextFrame = false;

    agfx::Device m_Device;
    agfx::CommandQueue m_CommandQueue;
    agfx::Fence m_Fence;
    agfx::SwapChain m_SwapChain;
    uint64 m_FenceValue;
    uint64 m_FrameSlot;
    uint64 m_FenceFrameSlots[FRAMES_IN_FLIGHT];
    agfx::CommandBuffer m_CommandBuffers[FRAMES_IN_FLIGHT];

    agfx::Texture m_DepthTexture;
    bool m_DepthNeedsInitialTransition = true;
    void CreateDepthTexture(uint32 width, uint32 height);

    TUnique<ImGuiRenderer> m_ImGuiRenderer;
    TUnique<SponzaRenderer> m_SponzaRenderer;
    TUnique<DebugRenderer> m_DebugRenderer;

    struct PendingMipTransition
    {
        agfxTexture* texture;
        uint32 mip;
    };
    TArray<PendingMipTransition> m_PendingMipTransitions;

    static void* Allocate(uint64 size);
    static void Free(void* ptr);
    static void* TempAllocate(uint64 size);
    static void TempFree(void* ptr);
    static void Log(agfxLogSeverity level, const char* message);
};
