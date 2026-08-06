/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 21:26:06
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Renderer.hpp"

#include <Caramel/Core/Logger.hpp>
#include <Caramel/Core/CpuProfiler.hpp>
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

    agfx::QueryPoolCreateInfo timingQueryPoolInfo = agfx::QueryPoolCreateInfo().SetCount(RenderGraph::kMaxTimedPasses * 2);
    for (uint64 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        m_FenceFrameSlots[i] = 0;
        m_CommandBuffers[i] = m_Device.CreateCommandBuffer(m_CommandQueue);
        m_TimingQueryPools[i] = m_Device.CreateQueryPool(m_CommandQueue, timingQueryPoolInfo);
    }

    CreateDepthTexture(m_ViewportWidth, m_ViewportHeight);
    CreateSceneColorTexture(m_ViewportWidth, m_ViewportHeight);
    CreateVisibilityTexture(m_ViewportWidth, m_ViewportHeight);
    CreateSceneLightingTexture(m_ViewportWidth, m_ViewportHeight);
    CreateGBufferTextures(m_ViewportWidth, m_ViewportHeight);
    CreateHZBTexture(m_ViewportWidth, m_ViewportHeight);

    ShaderServer::Initialize(m_Device, *this);

    // Schemes own the scene-geometry pipelines, so they must be discovered and compiled before
    // anything tries to draw with them.
    m_SchemeRegistry.LoadDirectory("Content/Materials/Schemes");

    m_GPUScene.Init(m_Device, m_CommandQueue, m_SchemeRegistry, (uint32)FRAMES_IN_FLIGHT);
    m_ImGuiRenderer = MakeUnique<ImGuiRenderer>(m_Device, m_CommandQueue, m_SwapChain.GetFormat(), (uint32)FRAMES_IN_FLIGHT);
    m_SceneRenderer = MakeUnique<SceneRenderer>(m_Device, m_SchemeRegistry, m_SwapChain.GetFormat(), kDepthFormat, (uint32)FRAMES_IN_FLIGHT);
    m_DebugRenderer = MakeUnique<DebugRenderer>(m_Device, m_SwapChain.GetFormat(), kDepthFormat, (uint32)FRAMES_IN_FLIGHT);
    m_AccelStructManager = MakeUnique<AccelerationStructureManager>(m_Device);

    m_Device.MakeResourcesResident();
}

void Renderer::CreateDepthTexture(uint32 width, uint32 height)
{
    // Sampled is what lets the HZB build read this in a compute shader. On D3D12 that also forces
    // the underlying resource typeless (see agfxTextureResourceDesc) so the SRV can be R32_FLOAT.
    agfx::TextureCreateInfo depthInfo;
    depthInfo.SetSize(width, height)
             .SetFormat(kDepthFormat)
             .SetType(agfx::TextureType::Texture2D)
             .SetUsage(agfx::TextureUsage::DepthStencilAttachment | agfx::TextureUsage::Sampled)
             .SetMipLevels(1);
    m_ImportedResourceState.Erase(m_DepthTexture.Get());
    m_DepthTexture = m_Device.CreateTexture(depthInfo);
    m_DepthTexture.SetName("Scene Depth Buffer");

    agfx::TextureViewCreateInfo depthViewInfo = agfx::TextureViewCreateInfo().SetTexture(m_DepthTexture)
                                                                            .SetFormat(kDepthFormat)
                                                                            .SetMipRange(0, 1)
                                                                            .SetWriteable(false);
    m_DepthView = m_Device.CreateTextureView(depthViewInfo);
    m_HZB.depthHandle = (uint32)m_DepthView.GetHandle();
    m_HZB.depthWidth = width;
    m_HZB.depthHeight = height;

    m_Device.MakeResourcesResident();
}

void Renderer::CreateHZBTexture(uint32 width, uint32 height)
{
    // Power-of-two dimensions, rounded *down* from the depth buffer. Every mip is then exactly half
    // the previous one with no rounding, which is what lets the downsampler's 64x64 tile grid line up
    // with mip 6 exactly and keeps the whole chain free of partial-tile edge cases. Rounding down
    // also means one mip-0 texel covers between one and two depth texels, so a 2x2 max at the scaled
    // coordinate always over-covers -- the safe direction, since a too-large max culls less.
    uint32 hzbWidth = 1;
    while (hzbWidth * 2 <= width)
        hzbWidth *= 2;
    uint32 hzbHeight = 1;
    while (hzbHeight * 2 <= height)
        hzbHeight *= 2;

    uint32 mipCount = 1;
    for (uint32 size = hzbWidth > hzbHeight ? hzbWidth : hzbHeight; size > 1; size /= 2)
        ++mipCount;
    if (mipCount > kMaxHZBMips)
        mipCount = kMaxHZBMips;

    agfx::TextureCreateInfo hzbInfo;
    hzbInfo.SetSize(hzbWidth, hzbHeight)
           .SetFormat(agfx::TextureFormat::R32F)
           .SetType(agfx::TextureType::Texture2D)
           .SetUsage(agfx::TextureUsage::Storage | agfx::TextureUsage::Sampled)
           .SetMipLevels(mipCount);
    m_ImportedResourceState.Erase(m_HZBTexture.Get());
    m_HZBTexture = m_Device.CreateTexture(hzbInfo);
    m_HZBTexture.SetName("Scene HZB");

    agfx::TextureViewCreateInfo readViewInfo = agfx::TextureViewCreateInfo().SetTexture(m_HZBTexture)
                                                                           .SetFormat(agfx::TextureFormat::R32F)
                                                                           .SetMipRange(0, mipCount)
                                                                           .SetWriteable(false);
    m_HZBView = m_Device.CreateTextureView(readViewInfo);

    m_HZB.hzbHandle = (uint32)m_HZBView.GetHandle();
    m_HZB.width = hzbWidth;
    m_HZB.height = hzbHeight;
    m_HZB.mipCount = mipCount;

    for (uint32 mip = 0; mip < kMaxHZBMips; ++mip) {
        // Slots past the real chain still need a valid handle: the downsampler's push constants
        // carry all kMaxHZBMips of them and the shader only guards on uMipCount at store time.
        uint32 viewMip = mip < mipCount ? mip : mipCount - 1;
        agfx::TextureViewCreateInfo mipViewInfo = agfx::TextureViewCreateInfo().SetTexture(m_HZBTexture)
                                                                               .SetFormat(agfx::TextureFormat::R32F)
                                                                               .SetMipRange(viewMip, 1)
                                                                               .SetWriteable(true);
        m_HZBMipViews[mip] = m_Device.CreateTextureView(mipViewInfo);
        m_HZB.mipHandles[mip] = (uint32)m_HZBMipViews[mip].GetHandle();
    }

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

void Renderer::CreateVisibilityTexture(uint32 width, uint32 height)
{
    agfx::TextureCreateInfo visibilityInfo;
    visibilityInfo.SetSize(width, height)
                  .SetFormat(agfx::TextureFormat::RG32Uint)
                  .SetType(agfx::TextureType::Texture2D)
                  .SetUsage(agfx::TextureUsage::ColorAttachment | agfx::TextureUsage::Sampled)
                  .SetMipLevels(1);
    m_ImportedResourceState.Erase(m_VisibilityTexture.Get());
    m_VisibilityTexture = m_Device.CreateTexture(visibilityInfo);
    m_VisibilityTexture.SetName("Visibility Buffer");

    agfx::TextureViewCreateInfo visibilityViewInfo = agfx::TextureViewCreateInfo().SetTexture(m_VisibilityTexture)
                                                                                  .SetFormat(agfx::TextureFormat::RG32Uint)
                                                                                  .SetMipRange(0, 1)
                                                                                  .SetWriteable(false);
    m_VisibilityView = m_Device.CreateTextureView(visibilityViewInfo);

    m_Device.MakeResourcesResident();
}

void Renderer::CreateSceneLightingTexture(uint32 width, uint32 height)
{
    agfx::TextureCreateInfo info;
    info.SetSize(width, height)
        .SetFormat(kSceneLightingFormat)
        .SetType(agfx::TextureType::Texture2D)
        .SetUsage(agfx::TextureUsage::ColorAttachment | agfx::TextureUsage::Sampled | agfx::TextureUsage::Storage)
        .SetMipLevels(1);
    m_ImportedResourceState.Erase(m_SceneLightingTexture.Get());
    m_SceneLightingTexture = m_Device.CreateTexture(info);
    m_SceneLightingTexture.SetName("Scene Lighting Buffer");

    agfx::TextureViewCreateInfo viewInfo = agfx::TextureViewCreateInfo().SetTexture(m_SceneLightingTexture)
                                                                        .SetFormat(kSceneLightingFormat)
                                                                        .SetMipRange(0, 1)
                                                                        .SetWriteable(false);
    m_SceneLightingView = m_Device.CreateTextureView(viewInfo);

    m_SceneLightingUAV = m_Device.CreateTextureView(viewInfo.SetWriteable(true));

    m_Device.MakeResourcesResident();
}

void Renderer::CreateGBufferTextures(uint32 width, uint32 height)
{
    constexpr agfx::TextureFormat formats[kGBufferTextureCount] = {
        agfx::TextureFormat::RGBA8Unorm,
        agfx::TextureFormat::RGBA16F,
        agfx::TextureFormat::RG8Unorm,
        agfx::TextureFormat::RGBA16F,
        agfx::TextureFormat::RG16F,
    };
    constexpr const char* names[kGBufferTextureCount] = {
        "GBuffer Albedo",
        "GBuffer Normal",
        "GBuffer Metallic Roughness",
        "GBuffer Emissive",
        "GBuffer Motion",
    };

    for (uint32 i = 0; i < kGBufferTextureCount; ++i) {
        agfx::TextureCreateInfo info;
        info.SetSize(width, height)
            .SetFormat(formats[i])
            .SetType(agfx::TextureType::Texture2D)
            .SetUsage(agfx::TextureUsage::ColorAttachment | agfx::TextureUsage::Sampled)
            .SetMipLevels(1);
        m_ImportedResourceState.Erase(m_GBufferTextures[i].Get());
        m_GBufferTextures[i] = m_Device.CreateTexture(info);
        m_GBufferTextures[i].SetName(names[i]);

        agfx::TextureViewCreateInfo viewInfo = agfx::TextureViewCreateInfo().SetTexture(m_GBufferTextures[i])
                                                                            .SetFormat(formats[i])
                                                                            .SetMipRange(0, 1)
                                                                            .SetWriteable(false);
        m_GBufferViews[i] = m_Device.CreateTextureView(viewInfo);
    }

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
    CreateVisibilityTexture(m_ViewportWidth, m_ViewportHeight);
    CreateSceneLightingTexture(m_ViewportWidth, m_ViewportHeight);
    CreateGBufferTextures(m_ViewportWidth, m_ViewportHeight);
    CreateDepthTexture(m_ViewportWidth, m_ViewportHeight);
    CreateHZBTexture(m_ViewportWidth, m_ViewportHeight);
    // The fresh pyramid holds garbage and the visibility flags now describe a different projection,
    // so both have to be rebuilt from scratch before anything culls against them.
    m_SceneRenderer->InvalidateOcclusionState();
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
    CARAMEL_ZONE("Renderer::Render");

    m_FrameSlot = (uint32_t)(m_FenceValue % FRAMES_IN_FLIGHT);
    {
        // Not CPU work -- this is the CPU sitting idle until the GPU finishes with this frame slot
        // from FRAMES_IN_FLIGHT frames ago. A spike here means the GPU fell behind (e.g. camera
        // movement changed visibility/LOD enough to cost more GPU time that frame), not that the CPU
        // did anything expensive. Removing the wait is not a fix: this frame slot's command buffer
        // and resources may still be in use by the GPU, so proceeding without it is a data race.
        // Real levers: raise FRAMES_IN_FLIGHT for more slack (costs latency/memory), or find out why
        // the GPU frame got more expensive via the GPU Timings panel for that same moment.
        CARAMEL_ZONE_WAIT("Fence Wait");
        m_Fence.Wait(m_FenceFrameSlots[m_FrameSlot]);
    }

    // The wait above already proves this slot's last Execute() (including its ResolveQueryPool) is
    // done, so it's safe to read the timestamps it wrote before this frame overwrites them below.
    if (m_TimingSlotHasData[m_FrameSlot] && !m_TimingSlotNames[m_FrameSlot].IsEmpty()) {
        uint32 count = (uint32)m_TimingSlotNames[m_FrameSlot].Size();
        uint64 timestamps[RenderGraph::kMaxTimedPasses * 2];
        m_TimingQueryPools[m_FrameSlot].Readback(0, count * 2, timestamps);

        m_LastPassTimings.Clear();
        for (uint32 i = 0; i < count; ++i) {
            float gpuTimeMs = (float)(timestamps[i * 2 + 1] - timestamps[i * 2]) / 1000000.0f;
            m_LastPassTimings.PushBack({ m_TimingSlotNames[m_FrameSlot][i], gpuTimeMs });
        }
    }

    ShaderServer::Tick();

    m_GPUScene.Build(streamingManager, renderInstances, (uint32)m_FrameSlot);
    {
        CARAMEL_ZONE("Scan Newly Resident Models");
        m_AccelStructManager->ScanForNewlyResidentModels(renderInstances);
    }

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
    RGTextureHandle hzbHandle = graph.ImportTexture("Scene HZB", m_HZBTexture, GetImportedState(m_HZBTexture.Get()));
    RGTextureHandle visibilityHandle = graph.ImportTexture("Visibility", m_VisibilityTexture, GetImportedState(m_VisibilityTexture.Get()));
    RGTextureHandle sceneLightingHandle = graph.ImportTexture("Scene Lighting", m_SceneLightingTexture, GetImportedState(m_SceneLightingTexture.Get()));
    constexpr const char* gbufferNames[kGBufferTextureCount] = {
        "GBuffer Albedo", "GBuffer Normal", "GBuffer Metallic Roughness", "GBuffer Emissive", "GBuffer Motion"
    };
    RGTextureHandle gbufferHandles[kGBufferTextureCount];
    for (uint32 i = 0; i < kGBufferTextureCount; ++i)
        gbufferHandles[i] = graph.ImportTexture(gbufferNames[i], m_GBufferTextures[i], GetImportedState(m_GBufferTextures[i].Get()));

    // Both cull dispatches read the camera constants, so they have to exist before the graph runs.
    m_SceneRenderer->BeginFrame(camera, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);

    graph.AddPass("Cull Early",
        [&](RGPassBuilder& builder) {
            // Touches no RG-tracked resource (the bundles are raw agfx objects with their own
            // manual barrier sequence), so it would otherwise be culled -- same reasoning as the
            // "Acceleration Structure Build" pass below.
            builder.AlwaysExecute();
        },
        [&](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_SceneRenderer->CullEarly(cmd, m_GPUScene, m_HZB, (uint32)m_FrameSlot);
        });

    graph.AddAttachmentPass("Scene Early",
        [&](RGPassBuilder& builder) {
            // Zero is the only portable clear value for a uint attachment (Vulkan reinterprets the
            // float bits) -- background pixels are detected via depth == 1.0 in the resolve, never
            // via a uint sentinel.
            RGAttachmentDesc colorAttachment{};
            colorAttachment.texture = visibilityHandle;
            colorAttachment.loadOp = agfx::LoadOp::Clear;
            colorAttachment.storeOp = agfx::StoreOp::Store;
            colorAttachment.clearColor[0] = 0.0f;
            colorAttachment.clearColor[1] = 0.0f;
            colorAttachment.clearColor[2] = 0.0f;
            colorAttachment.clearColor[3] = 0.0f;
            builder.AddColorAttachment(colorAttachment);

            RGAttachmentDesc depthAttachment{};
            depthAttachment.texture = depthHandle;
            depthAttachment.loadOp = agfx::LoadOp::Clear;
            depthAttachment.storeOp = agfx::StoreOp::Store;
            depthAttachment.clearDepth = 1.0f;
            builder.SetDepthAttachment(depthAttachment);

            // SceneAS occlusion-culls meshlets against whatever the pyramid holds at this point,
            // which is the one last frame left behind.
            builder.ReadTexture(hzbHandle, agfx::ResourceState::NonPixelShaderResource);
        },
        [&](agfx::RenderPass& pass, RGResolveContext&) {
            m_SceneRenderer->RenderEarly(pass, m_GPUScene, m_HZB, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);
        });

    graph.AddPass("Build HZB",
        [&](RGPassBuilder& builder) {
            builder.ReadTexture(depthHandle, agfx::ResourceState::NonPixelShaderResource);
            builder.WriteTexture(hzbHandle, agfx::ResourceState::UnorderedAccess);
            // The pyramid outlives the graph: next frame's early pass samples it before anything
            // rebuilds it, so it has to be left in a readable state, not in UnorderedAccess.
            builder.MarkAsExternallyRead(hzbHandle, agfx::ResourceState::NonPixelShaderResource);
        },
        [&](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_SceneRenderer->BuildHZB(cmd, m_HZB);
        });

    graph.AddPass("Cull Late",
        [&](RGPassBuilder& builder) {
            builder.ReadTexture(hzbHandle, agfx::ResourceState::NonPixelShaderResource);
            builder.AlwaysExecute();
        },
        [&](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_SceneRenderer->CullLate(cmd, m_GPUScene, m_HZB, (uint32)m_FrameSlot);
        });

    graph.AddAttachmentPass("Scene Late",
        [&](RGPassBuilder& builder) {
            RGAttachmentDesc colorAttachment{};
            colorAttachment.texture = visibilityHandle;
            colorAttachment.loadOp = agfx::LoadOp::Load;
            colorAttachment.storeOp = agfx::StoreOp::Store;
            builder.AddColorAttachment(colorAttachment);

            RGAttachmentDesc depthAttachment{};
            depthAttachment.texture = depthHandle;
            depthAttachment.loadOp = agfx::LoadOp::Load;
            depthAttachment.storeOp = agfx::StoreOp::Store;
            builder.SetDepthAttachment(depthAttachment);

            builder.ReadTexture(hzbHandle, agfx::ResourceState::NonPixelShaderResource);
        },
        [&](agfx::RenderPass& pass, RGResolveContext&) {
            m_SceneRenderer->RenderLate(pass, m_GPUScene, m_HZB, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);
        });

    graph.AddAttachmentPass("GBuffer Resolve",
        [&](RGPassBuilder& builder) {
            // Attachment order is the contract with the GBuffer Resolve pipeline (SceneRenderer)
            // and GBufferOut in GBufferResolve.hlsl: scene lighting first, then the gbuffer.
            RGAttachmentDesc sceneLightingAttachment{};
            sceneLightingAttachment.texture = sceneLightingHandle;
            sceneLightingAttachment.loadOp = agfx::LoadOp::Clear;
            sceneLightingAttachment.storeOp = agfx::StoreOp::Store;
            sceneLightingAttachment.clearColor[3] = 1.0f;
            builder.AddColorAttachment(sceneLightingAttachment);

            for (uint32 i = 0; i < kGBufferTextureCount; ++i) {
                RGAttachmentDesc attachment{};
                attachment.texture = gbufferHandles[i];
                attachment.loadOp = agfx::LoadOp::Clear;
                attachment.storeOp = agfx::StoreOp::Store;
                builder.AddColorAttachment(attachment);
            }

            builder.ReadTexture(visibilityHandle, agfx::ResourceState::PixelShaderResource);
            builder.ReadTexture(depthHandle, agfx::ResourceState::PixelShaderResource);
        },
        [&](agfx::RenderPass& pass, RGResolveContext&) {
            m_SceneRenderer->RenderGBufferResolve(pass, m_GPUScene, (uint32)m_VisibilityView.GetHandle(), m_HZB.depthHandle, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);
        });

    DeferredTargets deferredTargets{};
    deferredTargets.visibilityHandle = (uint32)m_VisibilityView.GetHandle();
    deferredTargets.depthHandle = m_HZB.depthHandle;
    deferredTargets.albedoHandle = (uint32)m_GBufferViews[0].GetHandle();
    deferredTargets.normalHandle = (uint32)m_GBufferViews[1].GetHandle();
    deferredTargets.metallicRoughnessHandle = (uint32)m_GBufferViews[2].GetHandle();
    deferredTargets.emissiveHandle = (uint32)m_GBufferViews[3].GetHandle();
    deferredTargets.sceneLightingUAVHandle = (uint32)m_SceneLightingUAV.GetHandle();

    graph.AddPass("Material Classify",
        [&](RGPassBuilder& builder) {
            builder.ReadTexture(visibilityHandle, agfx::ResourceState::NonPixelShaderResource);
            builder.ReadTexture(depthHandle, agfx::ResourceState::NonPixelShaderResource);
            // Its real outputs -- the classify buffer, the pixel list and the deferred bundle -- are
            // raw agfx objects with their own manual barrier sequence, invisible to the graph. Same
            // reasoning as the Cull passes above.
            builder.AlwaysExecute();
        },
        [&](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_SceneRenderer->ClassifyMaterials(cmd, m_GPUScene, deferredTargets, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);
        });

    graph.AddPass("Material Shade",
        [&](RGPassBuilder& builder) {
            for (uint32 i = 0; i < kGBufferTextureCount; ++i)
                builder.ReadTexture(gbufferHandles[i], agfx::ResourceState::NonPixelShaderResource);
            builder.ReadTexture(visibilityHandle, agfx::ResourceState::NonPixelShaderResource);
            builder.ReadTexture(depthHandle, agfx::ResourceState::NonPixelShaderResource);
            builder.WriteTexture(sceneLightingHandle, agfx::ResourceState::UnorderedAccess);
        },
        [&](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_SceneRenderer->ShadeMaterials(cmd, m_GPUScene, deferredTargets, m_ViewportWidth, m_ViewportHeight, (uint32)m_FrameSlot);
        });

    graph.AddAttachmentPass("Composite",
        [&](RGPassBuilder& builder) {
            RGAttachmentDesc sceneColorAttachment{};
            sceneColorAttachment.texture = sceneColorHandle;
            sceneColorAttachment.loadOp = agfx::LoadOp::Clear;
            sceneColorAttachment.storeOp = agfx::StoreOp::Store;
            sceneColorAttachment.clearColor[3] = 1.0f;
            builder.AddColorAttachment(sceneColorAttachment);

            builder.ReadTexture(sceneLightingHandle, agfx::ResourceState::PixelShaderResource);
        },
        [&](agfx::RenderPass& pass, RGResolveContext&) {
            m_SceneRenderer->RenderComposite(pass, (uint32)m_SceneLightingView.GetHandle(), m_ViewportWidth, m_ViewportHeight);
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
    agfx::QueryPool* computeQueryPool = nullptr;
    if (m_AccelStructManager->ShouldBuild()) {
        m_AccelStructManager->WaitForFrameSlot(m_FrameSlot);
        computeCommandBuffer = &m_AccelStructManager->GetFrameCommandBuffer(m_FrameSlot);
        computeQueryPool = &m_AccelStructManager->GetTimingQueryPool(m_FrameSlot);
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

    {
        CARAMEL_ZONE("RenderGraph Compile");
        graph.Compile();
    }
    {
        // Pass lambdas were only captured above -- their command-recording work (incl. cull/render
        // dispatch bodies) actually runs inside this call.
        CARAMEL_ZONE("RenderGraph Execute");
        graph.Execute(commandBuffer, &m_TimingQueryPools[m_FrameSlot], computeQueryPool);
    }

    SetImportedState(m_SceneColorTexture.Get(), graph.GetFinalState(sceneColorHandle));
    SetImportedState(m_DepthTexture.Get(), graph.GetFinalState(depthHandle));
    SetImportedState(m_HZBTexture.Get(), graph.GetFinalState(hzbHandle));
    SetImportedState(m_VisibilityTexture.Get(), graph.GetFinalState(visibilityHandle));
    SetImportedState(m_SceneLightingTexture.Get(), graph.GetFinalState(sceneLightingHandle));
    for (uint32 i = 0; i < kGBufferTextureCount; ++i)
        SetImportedState(m_GBufferTextures[i].Get(), graph.GetFinalState(gbufferHandles[i]));
    m_LastGraphDebugInfo = graph.GetDebugInfo();
    m_TimingSlotNames[m_FrameSlot] = graph.GetTimedPassNames();
    m_TimingSlotHasData[m_FrameSlot] = true;
    if (computeCommandBuffer)
        m_AccelStructManager->SetLastTimedPassNames(m_FrameSlot, graph.GetTimedComputePassNames());

    if (computeCommandBuffer) {
        computeCommandBuffer->End();
        m_AccelStructManager->Submit(*computeCommandBuffer);
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
