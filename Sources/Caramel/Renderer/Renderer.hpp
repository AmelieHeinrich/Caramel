/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 21:25:29
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/NativeHandle.hpp>

#include <AGFX/agfx.hpp>
#include <SDL3/SDL.h>

class ImGuiRenderer;

class Renderer
{
public:
    Renderer(SDL_Window* window);
    ~Renderer();

    void Render();
    void Resize();

    // Blocks until the GPU has retired every frame submitted so far (across all FrameCount slots),
    // unlike the per-frame-slot wait in Render(). Only safe/necessary point to destroy a pipeline
    // that might still be bound by an in-flight command buffer from another slot -- used by
    // ShaderServer::Tick() when applying a hot-reloaded pipeline swap.
    void WaitIdle() { m_Fence.Wait(m_FenceValue); }
public:
    static constexpr uint64 FrameCount = 3;

private:
    SDL_Window* m_Window;
    TUnique<NativeHandle> m_NativeHandle;
    bool m_ResizeNextFrame = false;

    agfx::Device m_Device;
    agfx::CommandQueue m_CommandQueue;
    agfx::Fence m_Fence;
    agfx::SwapChain m_SwapChain;
    uint64 m_FenceValue;
    uint64 m_FrameSlot;
    uint64 m_FenceFrameSlots[FrameCount];
    agfx::CommandBuffer m_CommandBuffers[FrameCount];

    TUnique<ImGuiRenderer> m_ImGuiRenderer;

    static void* Allocate(uint64 size);
    static void Free(void* ptr);
    static void* TempAllocate(uint64 size);
    static void TempFree(void* ptr);
    static void Log(agfxLogSeverity level, const char* message);
};
