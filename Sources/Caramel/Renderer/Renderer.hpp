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
#include <Caramel/Renderer/MaterialScheme.hpp>
#include <Caramel/Scene/GPUScene.hpp>
#include <Caramel/Scene/RenderInstance.hpp>

#include <AGFX/agfx.hpp>
#include <SDL3/SDL.h>

#include <imgui.h>

class DebugRenderer;
class ImGuiRenderer;
class SceneRenderer;
class StreamingManager;

class Renderer
{
public:
    Renderer(SDL_Window* window);
    ~Renderer();

    void Render(const Camera& camera, StreamingManager& streamingManager, const TArray<RenderInstance>& renderInstances);
    void Resize();
    void WaitIdle() { m_Fence.Wait(m_FenceValue); }

    void SetViewportSize(uint32 width, uint32 height);
    ImTextureID GetViewportTextureID() const { return m_SceneColorTexID; }

    void EnqueueMipTransition(agfx::Texture& texture, uint32 mip);

    agfx::Device& GetDevice() { return m_Device; }
    agfxDeviceInfo GetDeviceInfo() const { return m_Device.GetInfo(); }

    const SchemeRegistry& GetSchemeRegistry() const { return m_SchemeRegistry; }
    const GPUScene& GetGPUScene() const { return m_GPUScene; }

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

    agfx::Texture m_SceneColorTexture;
    agfx::TextureView m_SceneColorView;
    ImTextureID m_SceneColorTexID = ImTextureID_Invalid;
    uint32 m_ViewportWidth = 1;
    uint32 m_ViewportHeight = 1;
    bool m_SceneColorNeedsInitialTransition = true;
    void CreateSceneColorTexture(uint32 width, uint32 height);

    SchemeRegistry m_SchemeRegistry;
    GPUScene m_GPUScene;

    TUnique<ImGuiRenderer> m_ImGuiRenderer;
    TUnique<SceneRenderer> m_SceneRenderer;
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
