/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 21:26:06
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Renderer.hpp"

#include <Caramel/Core/Logger.hpp>
#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <Caramel/Renderer/ImGuiRenderer.hpp>

#include <imgui.h>

Renderer* Renderer::s_Instance = nullptr;

Renderer::Renderer(SDL_Window* window)
    : m_Window(window)
    , m_FenceValue(0)
    , m_FrameSlot(0)
{
    s_Instance = this;

    agfxDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.displayServerProtocol = AGFX_DISPLAY_SERVER_PROTOCOL_WAYLAND;
    deviceCreateInfo.enableValidation = true;
    deviceCreateInfo.allocate = Allocate;
    deviceCreateInfo.free = Free;
    deviceCreateInfo.tempAllocate = TempAllocate;
    deviceCreateInfo.tempFree = TempFree;
    deviceCreateInfo.logFunction = Log;

    m_Device = agfx::Device(deviceCreateInfo);
    m_CommandQueue = m_Device.CreateCommandQueue(agfx::CommandQueueType::Graphics);
    m_Fence = m_Device.CreateFence();
    m_NativeHandle = MakeUnique<NativeHandle>(m_Window);

    int32 width, height;
    SDL_GetWindowSizeInPixels(m_Window, &width, &height);

    agfx::SwapChainCreateInfo swapChainCreateInfo{};
    swapChainCreateInfo.width = width;
    swapChainCreateInfo.height = height;
    swapChainCreateInfo.imageCount = FRAMES_IN_FLIGHT;
    swapChainCreateInfo.queue = m_CommandQueue;
    swapChainCreateInfo.vsync = false;
    swapChainCreateInfo.isHDR = false;

    m_SwapChain = m_NativeHandle->CreateSwapChain(m_Device, swapChainCreateInfo);

    for (uint64 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        m_FenceFrameSlots[i] = 0;
        m_CommandBuffers[i] = m_Device.CreateCommandBuffer(m_CommandQueue);
    }

    ShaderServer::Initialize(m_Device, *this);
    m_ImGuiRenderer = MakeUnique<ImGuiRenderer>(m_Device, m_CommandQueue, m_SwapChain.GetFormat(), (uint32)FRAMES_IN_FLIGHT);
}

Renderer::~Renderer()
{
    ShaderServer::Shutdown();
}

void Renderer::Render()
{
    m_FrameSlot = (uint32_t)(m_FenceValue % FRAMES_IN_FLIGHT);
    m_Fence.Wait(m_FenceFrameSlots[m_FrameSlot]);

    ShaderServer::Tick();

    if (m_ResizeNextFrame) {
        int32 width, height;
        SDL_GetWindowSizeInPixels(m_Window, &width, &height);

        m_SwapChain.Resize(width, height);
        m_ResizeNextFrame = false;
    }

    agfx::CommandBuffer& commandBuffer = m_CommandBuffers[m_FrameSlot];
    commandBuffer.Reset();
    commandBuffer.Begin();

    agfx::Texture backBuffer = m_SwapChain.AcquireNextTexture();
    commandBuffer.TextureBarrier(backBuffer, agfx::ResourceState::Present, agfx::ResourceState::RenderTarget);

    agfx::RenderTargetCreateInfo renderTargetCreateInfo{};
    renderTargetCreateInfo.texture = backBuffer;
    agfx::RenderTarget renderTarget = m_Device.CreateRenderTarget(renderTargetCreateInfo);
    
    agfx::RenderPassCreateInfo renderPassCreateInfo{};
    renderPassCreateInfo.colorAttachmentCount = 1;
    renderPassCreateInfo.colorAttachments[0].renderTarget = renderTarget;
    renderPassCreateInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_CLEAR;
    renderPassCreateInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
    renderPassCreateInfo.colorAttachments[0].clearColor[0] = 0.1f;
    renderPassCreateInfo.colorAttachments[0].clearColor[1] = 0.1f;
    renderPassCreateInfo.colorAttachments[0].clearColor[2] = 0.1f;
    renderPassCreateInfo.colorAttachments[0].clearColor[3] = 1.0f;
    renderPassCreateInfo.name = "Main Render Pass";

    agfx::RenderPass renderPass = commandBuffer.BeginRenderPass(renderPassCreateInfo);

    int32 width, height;
    SDL_GetWindowSizeInPixels(m_Window, &width, &height);
    m_ImGuiRenderer->RenderDrawData(ImGui::GetDrawData(), renderPass, (uint32)width, (uint32)height, (uint32)m_FrameSlot);

    renderPass.End();

    commandBuffer.TextureBarrier(backBuffer, agfx::ResourceState::RenderTarget, agfx::ResourceState::Present);
    commandBuffer.End();
    m_CommandQueue.Submit(commandBuffer);
    m_SwapChain.Present();

    m_FenceFrameSlots[m_FrameSlot] = ++m_FenceValue;
    m_CommandQueue.Signal(m_Fence, m_FenceValue);
}

void Renderer::Resize()
{
    m_ResizeNextFrame = true;
}

void* Renderer::Allocate(uint64 size)
{
    return malloc(size);
}

void Renderer::Free(void* ptr)
{
    free(ptr);
}

void* Renderer::TempAllocate(uint64 size)
{
    return malloc(size);
}

void Renderer::TempFree(void* ptr)
{
    free(ptr);
}

void Renderer::Log(agfxLogSeverity level, const char* message)
{
    switch (level) {
        case AGFX_LOG_SEVERITY_INFO:
            CARAMEL_INFO("AGFX: {}", message);
            break;
        case AGFX_LOG_SEVERITY_WARNING:
            CARAMEL_WARN("AGFX: {}", message);
            break;
        case AGFX_LOG_SEVERITY_ERROR:
            CARAMEL_ERROR("AGFX: {}", message);
            break;
        default:
            CARAMEL_ERROR("AGFX: Unknown log severity level");
            break;
    }
}
