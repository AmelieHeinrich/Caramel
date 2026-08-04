/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 21:26:06
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Renderer.hpp"

#include <Caramel/Core/Logger.hpp>
#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <Caramel/Renderer/AccelerationStructureManager.hpp>
#include <Caramel/Renderer/DebugRenderer.hpp>
#include <Caramel/Renderer/ImGuiRenderer.hpp>
#include <Caramel/Renderer/SceneRenderer.hpp>

#include <imgui.h>

namespace
{
    constexpr agfx::TextureFormat kDepthFormat = agfx::TextureFormat::Depth32F;
}

Renderer* Renderer::s_Instance = nullptr;

Renderer::Renderer(SDL_Window* window, bool vsync)
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
    m_RenderGraphAllocator = MakeUnique<RenderGraphAllocator>(m_Device);

    int32 width, height;
    SDL_GetWindowSizeInPixels(m_Window, &width, &height);

    agfx::SwapChainCreateInfo swapChainCreateInfo{};
    swapChainCreateInfo.width = width;
    swapChainCreateInfo.height = height;
    swapChainCreateInfo.imageCount = FRAMES_IN_FLIGHT;
    swapChainCreateInfo.queue = m_CommandQueue;
    swapChainCreateInfo.vsync = vsync;
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
    m_AccelStructManager = MakeUnique<AccelerationStructureManager>(m_Device);

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
    m_ImportedResourceState.Erase(m_DepthTexture.Get());
    m_DepthTexture = m_Device.CreateTexture(depthInfo);
    m_DepthTexture.SetName("Scene Depth Buffer");

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
    m_ImportedResourceState.Erase(m_SceneColorTexture.Get());
    m_SceneColorTexture = m_Device.CreateTexture(colorInfo);
    m_SceneColorTexture.SetName("Scene Color Buffer");

    agfx::TextureViewCreateInfo colorViewInfo = agfx::TextureViewCreateInfo().SetTexture(m_SceneColorTexture)
                                                                              .SetFormat(m_SwapChain.GetFormat())
                                                                              .SetMipRange(0, 1)
                                                                              .SetWriteable(false);
    m_SceneColorView = m_Device.CreateTextureView(colorViewInfo);
    m_SceneColorTexID = (ImTextureID)(intptr_t)m_SceneColorView.GetHandle();

    m_Device.MakeResourcesResident();
}

void Renderer::SetViewportSize(uint32 width, uint32 height)
{
    m_RequestedViewportWidth = width > 0 ? width : 1;
    m_RequestedViewportHeight = height > 0 ? height : 1;
}

void Renderer::PollViewportResize()
{
    if (m_RequestedViewportWidth == m_ViewportWidth && m_RequestedViewportHeight == m_ViewportHeight)
        return;

    m_ViewportWidth = m_RequestedViewportWidth;
    m_ViewportHeight = m_RequestedViewportHeight;

    m_Device.WaitIdle();
    CreateSceneColorTexture(m_ViewportWidth, m_ViewportHeight);
    CreateDepthTexture(m_ViewportWidth, m_ViewportHeight);
}

Renderer::~Renderer()
{
    m_Device.WaitIdle();
    ShaderServer::Shutdown();
}

agfx::ResourceState Renderer::GetImportedState(agfxTexture* texture) const
{
    auto it = m_ImportedResourceState.Find(texture);
    return it != m_ImportedResourceState.End() ? it->second : agfx::ResourceState::Common;
}

void Renderer::SetImportedState(agfxTexture* texture, agfx::ResourceState state)
{
    m_ImportedResourceState[texture] = state;
}

void Renderer::Render(const Camera& camera, StreamingManager& streamingManager, const TArray<RenderInstance>& renderInstances)
{
    m_FrameSlot = (uint32_t)(m_FenceValue % FRAMES_IN_FLIGHT);
    m_Fence.Wait(m_FenceFrameSlots[m_FrameSlot]);

    ShaderServer::Tick();

    m_GPUScene.Build(streamingManager, renderInstances, (uint32)m_FrameSlot);
    m_AccelStructManager->ScanForNewlyResidentModels(renderInstances);

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

    // Stays outside the graph, and must run before any graph barrier in this same command buffer:
    // textures imported below must reflect this *post*-transition state, not their raw pre-frame one.
    for (const PendingMipTransition& transition : m_PendingMipTransitions)
        agfxCommandBufferTextureBarrier(commandBuffer, transition.texture, AGFX_RESOURCE_STATE_COPY_DEST, AGFX_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, transition.mip, 0, 1);
    m_PendingMipTransitions.Clear();

    agfx::Texture backBuffer = m_SwapChain.AcquireNextTexture();

    RenderGraph graph(m_Device, m_RenderGraphAllocator.get());

    RGTextureHandle sceneColorHandle = graph.ImportTexture("Scene Color", m_SceneColorTexture, GetImportedState(m_SceneColorTexture.Get()));
    RGTextureHandle depthHandle = graph.ImportTexture("Scene Depth", m_DepthTexture, GetImportedState(m_DepthTexture.Get()));
    RGTextureHandle backbufferHandle = graph.ImportTexture("Back Buffer", backBuffer, agfx::ResourceState::Present);

    graph.AddAttachmentPass("Scene Pass",
        [&](RGPassBuilder& builder) {
            RGAttachmentDesc colorAttachment{};
            colorAttachment.texture = sceneColorHandle;
            colorAttachment.loadOp = agfx::LoadOp::Clear;
            colorAttachment.storeOp = agfx::StoreOp::Store;
            colorAttachment.clearColor[0] = 0.1f;
            colorAttachment.clearColor[1] = 0.1f;
            colorAttachment.clearColor[2] = 0.1f;
            colorAttachment.clearColor[3] = 1.0f;
            builder.AddColorAttachment(colorAttachment);

            RGAttachmentDesc depthAttachment{};
            depthAttachment.texture = depthHandle;
            depthAttachment.loadOp = agfx::LoadOp::Clear;
            depthAttachment.storeOp = agfx::StoreOp::Store;
            depthAttachment.clearDepth = 1.0f;
            builder.SetDepthAttachment(depthAttachment);
        },
        [&](agfx::RenderPass& pass, RGResolveContext&) {
            m_SceneRenderer->Render(pass, m_GPUScene, m_SchemeRegistry, camera, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);
        });

    graph.AddPass("Debug Draw",
        [&](RGPassBuilder& builder) {
            builder.WriteTexture(sceneColorHandle, agfx::ResourceState::RenderTarget);
            builder.WriteTexture(depthHandle, agfx::ResourceState::DepthWrite);
            // Scene-color is sampled by ImGui next frame through a raw bindless handle
            // (GetViewportTextureID()), entirely outside the graph -- see RGTextureDesc::externallyRead.
            builder.MarkAsExternallyRead(sceneColorHandle, agfx::ResourceState::PixelShaderResource);
        },
        [&](agfx::CommandBuffer& cmd, RGResolveContext& ctx) {
            agfx::RenderTarget& colorTarget = ctx.ResolveRenderTarget(sceneColorHandle, false);
            agfx::RenderTarget& depthTarget = ctx.ResolveRenderTarget(depthHandle, true);
            m_DebugRenderer->Flush(cmd, colorTarget, depthTarget, camera, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);
        });

    graph.AddAttachmentPass("Swapchain Pass",
        [&](RGPassBuilder& builder) {
            builder.MarkSwapchainEdge(backbufferHandle);
            builder.MarkAsExternallyRead(backbufferHandle, agfx::ResourceState::Present);

            RGAttachmentDesc colorAttachment{};
            colorAttachment.texture = backbufferHandle;
            colorAttachment.loadOp = agfx::LoadOp::Clear;
            colorAttachment.storeOp = agfx::StoreOp::Store;
            colorAttachment.clearColor[0] = 0.0f;
            colorAttachment.clearColor[1] = 0.0f;
            colorAttachment.clearColor[2] = 0.0f;
            colorAttachment.clearColor[3] = 0.0f;
            builder.AddColorAttachment(colorAttachment);
        },
        [&](agfx::RenderPass& pass, RGResolveContext&) {
            m_ImGuiRenderer->RenderDrawData(ImGui::GetDrawData(), pass, (uint32)width, (uint32)height, (uint32)m_FrameSlot);
        });

    agfx::CommandBuffer* computeCommandBuffer = nullptr;
    if (m_AccelStructManager->IsSupported()) {
        m_AccelStructManager->WaitForFrameSlot(m_FrameSlot);
        computeCommandBuffer = &m_AccelStructManager->GetFrameCommandBuffer(m_FrameSlot);
        computeCommandBuffer->Reset();
        computeCommandBuffer->Begin();
        graph.SetQueueCommandBuffer(RGQueue::Compute, computeCommandBuffer);

        graph.AddPass("Acceleration Structure Build",
            [&](RGPassBuilder& builder) {
                builder.SetQueue(RGQueue::Compute);
                builder.AlwaysExecute();
            },
            [&](agfx::CommandBuffer& cmd, RGResolveContext&) {
                m_AccelStructManager->RecordBuilds(cmd, renderInstances);
            });
    }

    graph.Compile();
    graph.Execute(commandBuffer);

    SetImportedState(m_SceneColorTexture.Get(), graph.GetFinalState(sceneColorHandle));
    SetImportedState(m_DepthTexture.Get(), graph.GetFinalState(depthHandle));
    m_LastGraphDebugInfo = graph.GetDebugInfo();

    if (computeCommandBuffer) {
        computeCommandBuffer->End();
        m_AccelStructManager->Submit(*computeCommandBuffer, m_FrameSlot);
    }

    commandBuffer.End();
    m_CommandQueue.Submit(commandBuffer);
    m_SwapChain.Present();

    m_FenceFrameSlots[m_FrameSlot] = ++m_FenceValue;
    m_CommandQueue.Signal(m_Fence, m_FenceValue);

    PollViewportResize();
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
