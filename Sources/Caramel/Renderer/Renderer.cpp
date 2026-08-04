/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 21:26:06
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Renderer.hpp"

#include <Caramel/Core/Logger.hpp>
#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <Caramel/Renderer/DebugRenderer.hpp>
#include <Caramel/Renderer/ImGuiRenderer.hpp>
#include <Caramel/Renderer/SceneRenderer.hpp>

#include <imgui.h>

namespace
{
    constexpr agfx::TextureFormat kDepthFormat = agfx::TextureFormat::Depth32F;
}

Renderer* Renderer::s_Instance = nullptr;

Renderer::Renderer(SDL_Window* window)
    : m_Window(window)
    , m_FenceValue(0)
    , m_FrameSlot(0)
{
    s_Instance = this;

    agfxDeviceCreateInfo deviceCreateInfo{};
#if defined(CARAMEL_LINUX)
    deviceCreateInfo.displayServerProtocol = SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0
        ? AGFX_DISPLAY_SERVER_PROTOCOL_WAYLAND
        : AGFX_DISPLAY_SERVER_PROTOCOL_X11;
#endif
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

    CreateDepthTexture(m_ViewportWidth, m_ViewportHeight);
    CreateSceneColorTexture(m_ViewportWidth, m_ViewportHeight);

    ShaderServer::Initialize(m_Device, *this);

    // Schemes own the scene-geometry pipelines, so they must be discovered and compiled before
    // anything tries to draw with them.
    m_SchemeRegistry.LoadDirectory("Content/Materials/Schemes", m_SwapChain.GetFormat(), kDepthFormat);

    m_GPUScene.Init(m_Device, m_SchemeRegistry, (uint32)FRAMES_IN_FLIGHT);
    m_ImGuiRenderer = MakeUnique<ImGuiRenderer>(m_Device, m_CommandQueue, m_SwapChain.GetFormat(), (uint32)FRAMES_IN_FLIGHT);
    m_SceneRenderer = MakeUnique<SceneRenderer>(m_Device, (uint32)FRAMES_IN_FLIGHT);
    m_DebugRenderer = MakeUnique<DebugRenderer>(m_Device, m_SwapChain.GetFormat(), kDepthFormat, (uint32)FRAMES_IN_FLIGHT);

    m_Device.MakeResourcesResident();
}

void Renderer::CreateDepthTexture(uint32 width, uint32 height)
{
    agfx::TextureCreateInfo depthInfo;
    depthInfo.SetSize(width, height)
             .SetFormat(kDepthFormat)
             .SetType(agfx::TextureType::Texture2D)
             .SetUsage(agfx::TextureUsage::DepthStencilAttachment)
             .SetMipLevels(1);
    m_DepthTexture = m_Device.CreateTexture(depthInfo);
    m_DepthTexture.SetName("Scene Depth Buffer");
    m_DepthNeedsInitialTransition = true;

    m_Device.MakeResourcesResident();
}

void Renderer::CreateSceneColorTexture(uint32 width, uint32 height)
{
    agfx::TextureCreateInfo colorInfo;
    colorInfo.SetSize(width, height)
             .SetFormat(m_SwapChain.GetFormat())
             .SetType(agfx::TextureType::Texture2D)
             .SetUsage(agfx::TextureUsage::ColorAttachment | agfx::TextureUsage::Sampled)
             .SetMipLevels(1);
    m_SceneColorTexture = m_Device.CreateTexture(colorInfo);
    m_SceneColorTexture.SetName("Scene Color Buffer");

    agfx::TextureViewCreateInfo colorViewInfo = agfx::TextureViewCreateInfo().SetTexture(m_SceneColorTexture)
                                                                              .SetFormat(m_SwapChain.GetFormat())
                                                                              .SetMipRange(0, 1)
                                                                              .SetWriteable(false);
    m_SceneColorView = m_Device.CreateTextureView(colorViewInfo);
    m_SceneColorTexID = (ImTextureID)(intptr_t)m_SceneColorView.GetHandle();
    m_SceneColorNeedsInitialTransition = true;

    m_Device.MakeResourcesResident();
}

void Renderer::SetViewportSize(uint32 width, uint32 height)
{
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    if (width == m_ViewportWidth && height == m_ViewportHeight)
        return;

    m_ViewportWidth = width;
    m_ViewportHeight = height;

    m_Device.WaitIdle();
    CreateSceneColorTexture(m_ViewportWidth, m_ViewportHeight);
    CreateDepthTexture(m_ViewportWidth, m_ViewportHeight);
}

Renderer::~Renderer()
{
    m_Device.WaitIdle();
    ShaderServer::Shutdown();
}

void Renderer::Render(const Camera& camera, StreamingManager& streamingManager, const TArray<RenderInstance>& renderInstances)
{
    m_FrameSlot = (uint32_t)(m_FenceValue % FRAMES_IN_FLIGHT);
    m_Fence.Wait(m_FenceFrameSlots[m_FrameSlot]);

    ShaderServer::Tick();

    // Safe to rewrite this frame slot's buffers: the fence wait above retired the frame that last
    // used them. Streaming has already settled for this frame (Application updates it before
    // BuildRenderInstances), so the bindless handles collected here are final.
    m_GPUScene.Build(streamingManager, renderInstances, (uint32)m_FrameSlot);

    int32 width, height;
    SDL_GetWindowSizeInPixels(m_Window, &width, &height);
    if (m_ResizeNextFrame) {
        m_Device.WaitIdle();
        m_SwapChain.Resize(width, height);
        m_ResizeNextFrame = false;
    }

    agfx::CommandBuffer& commandBuffer = m_CommandBuffers[m_FrameSlot];
    commandBuffer.Reset();
    commandBuffer.Begin();

    for (const PendingMipTransition& transition : m_PendingMipTransitions)
        agfxCommandBufferTextureBarrier(commandBuffer, transition.texture, AGFX_RESOURCE_STATE_COPY_DEST, AGFX_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, transition.mip, 0, 1);
    m_PendingMipTransitions.Clear();

    agfx::RenderTargetCreateInfo sceneColorTargetCreateInfo{};
    sceneColorTargetCreateInfo.texture = m_SceneColorTexture;
    agfx::RenderTarget sceneColorTarget = m_Device.CreateRenderTarget(sceneColorTargetCreateInfo);

    agfx::RenderTargetCreateInfo depthTargetCreateInfo{};
    depthTargetCreateInfo.texture = m_DepthTexture;
    depthTargetCreateInfo.SetIsDepth(true);
    agfx::RenderTarget depthTarget = m_Device.CreateRenderTarget(depthTargetCreateInfo);

    if (m_DepthNeedsInitialTransition) {
        commandBuffer.TextureBarrier(m_DepthTexture, agfx::ResourceState::Common, agfx::ResourceState::DepthWrite);
        m_DepthNeedsInitialTransition = false;
    }

    if (m_SceneColorNeedsInitialTransition) {
        commandBuffer.TextureBarrier(m_SceneColorTexture, agfx::ResourceState::Common, agfx::ResourceState::RenderTarget);
        m_SceneColorNeedsInitialTransition = false;
    } else {
        commandBuffer.TextureBarrier(m_SceneColorTexture, agfx::ResourceState::PixelShaderResource, agfx::ResourceState::RenderTarget);
    }

    {
        agfx::RenderPassCreateInfo scenePassInfo{};
        scenePassInfo.colorAttachmentCount = 1;
        scenePassInfo.colorAttachments[0].renderTarget = sceneColorTarget;
        scenePassInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_CLEAR;
        scenePassInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
        scenePassInfo.colorAttachments[0].clearColor[0] = 0.1f;
        scenePassInfo.colorAttachments[0].clearColor[1] = 0.1f;
        scenePassInfo.colorAttachments[0].clearColor[2] = 0.1f;
        scenePassInfo.colorAttachments[0].clearColor[3] = 1.0f;
        scenePassInfo.hasDepthAttachment = 1;
        scenePassInfo.depthAttachment.renderTarget = depthTarget;
        scenePassInfo.depthAttachment.loadOp = AGFX_LOAD_OPERATION_CLEAR;
        scenePassInfo.depthAttachment.storeOp = AGFX_STORE_OPERATION_STORE;
        scenePassInfo.depthAttachment.clearDepth = 1.0f;
        scenePassInfo.name = "Scene Pass";
        scenePassInfo.width = m_ViewportWidth;
        scenePassInfo.height = m_ViewportHeight;

        agfx::RenderPass scenePass = commandBuffer.BeginRenderPass(scenePassInfo);
        m_SceneRenderer->Render(scenePass, m_GPUScene, m_SchemeRegistry, camera, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);
        scenePass.End();
    }

    m_DebugRenderer->Flush(commandBuffer, sceneColorTarget, depthTarget, camera, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);

    commandBuffer.TextureBarrier(m_SceneColorTexture, agfx::ResourceState::RenderTarget, agfx::ResourceState::PixelShaderResource);

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
    renderPassCreateInfo.name = "Main Render Pass";
    renderPassCreateInfo.width = width;
    renderPassCreateInfo.height = height;

    agfx::RenderPass renderPass = commandBuffer.BeginRenderPass(renderPassCreateInfo);

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

void Renderer::EnqueueMipTransition(agfx::Texture& texture, uint32 mip)
{
    m_PendingMipTransitions.PushBack({ texture, mip });
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
