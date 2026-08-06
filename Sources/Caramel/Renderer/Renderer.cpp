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
#include <Caramel/Renderer/Passes/VisibilityPasses.hpp>
#include <Caramel/Renderer/Passes/GBufferResolvePass.hpp>
#include <Caramel/Renderer/Passes/DeferredShadingPasses.hpp>
#include <Caramel/Renderer/Passes/CompositePass.hpp>
#include <Caramel/Renderer/Passes/DebugDrawPass.hpp>
#include <Caramel/Renderer/Passes/ImGuiPass.hpp>
#include <Caramel/Renderer/Passes/AccelStructPass.hpp>

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

    BuildPassList();

    m_Device.MakeResourcesResident();
}

// The frame, in order. Adding a technique is a new RenderPass subclass and one line here.
void Renderer::BuildPassList()
{
    m_Passes.PushBack(MakeUnique<VisibilityPasses>(*m_SceneRenderer));
    m_Passes.PushBack(MakeUnique<GBufferResolvePass>(*m_SceneRenderer));
    m_Passes.PushBack(MakeUnique<DeferredShadingPasses>(*m_SceneRenderer));
    m_Passes.PushBack(MakeUnique<CompositePass>(*m_SceneRenderer));
    m_Passes.PushBack(MakeUnique<DebugDrawPass>(*m_DebugRenderer));
    m_Passes.PushBack(MakeUnique<ImGuiPass>(*m_ImGuiRenderer));

    // Last in the list but on the async compute queue, so it overlaps the graphics work above rather
    // than following it. Renderer still owns its command buffer and submission -- see AccelStructPass.
    m_Passes.PushBack(MakeUnique<AccelStructPass>(*m_AccelStructManager));
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

// Every intra-frame target, declared as a graph transient. Ordering and formats are unchanged from
// when these were persistent textures; what changed is that the graph now sizes them, aliases their
// memory against each other, and owns their bindless views.
void Renderer::DeclareFrameTargets(RenderGraph& graph, FrameContext& ctx)
{
    auto viewportTexture = [&](agfx::TextureFormat format, agfx::TextureUsage usage) {
        return agfx::TextureCreateInfo().SetSize(ctx.width, ctx.height)
                                        .SetFormat(format)
                                        .SetType(agfx::TextureType::Texture2D)
                                        .SetUsage(usage)
                                        .SetMipLevels(1);
    };

    constexpr agfx::TextureUsage kTargetUsage = agfx::TextureUsage::ColorAttachment | agfx::TextureUsage::Sampled;

    // R32 = draw word (instance | LOD | fade), G32 = meshletIndex << 7 | triangleIndex. Written by
    // Scene Early/Late, consumed by GBuffer Resolve -- see VisBuffer.hlsl.
    ctx.visibility = graph.CreateTexture("Visibility", viewportTexture(agfx::TextureFormat::RG32Uint, kTargetUsage));

    // HDR shading output, sitting between the gbuffer and scene color. Deliberately not scene color
    // itself: the deferred shading passes write it from compute, and scene color carries the swap
    // chain's format -- a typed UAV on BGRA8Unorm is an optional D3D12 feature. Storage is what lets
    // the scheme dispatches take a writeable bindless view of it.
    ctx.sceneLighting = graph.CreateTexture("Scene Lighting",
        viewportTexture(kSceneLightingFormat, kTargetUsage | agfx::TextureUsage::Storage));

    // Order is the attachment contract with the GBuffer Resolve pipeline and GBufferOut in
    // GBufferResolve.hlsl: albedo, normal, metallic/roughness, emissive, motion.
    constexpr agfx::TextureFormat kGBufferFormats[kGBufferTextureCount] = {
        agfx::TextureFormat::RGBA8Unorm,
        agfx::TextureFormat::RGBA16F,
        agfx::TextureFormat::RG8Unorm,
        agfx::TextureFormat::RGBA16F,
        agfx::TextureFormat::RG16F,
    };
    constexpr const char* kGBufferNames[kGBufferTextureCount] = {
        "GBuffer Albedo",
        "GBuffer Normal",
        "GBuffer Metallic Roughness",
        "GBuffer Emissive",
        "GBuffer Motion",
    };
    for (uint32 i = 0; i < kGBufferTextureCount; ++i)
        ctx.gbuffer[i] = graph.CreateTexture(kGBufferNames[i], viewportTexture(kGBufferFormats[i], kTargetUsage));
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
    CreateHZBTexture(m_ViewportWidth, m_ViewportHeight);
    // The fresh pyramid holds garbage and the visibility flags now describe a different projection,
    // so both have to be rebuilt from scratch before anything culls against them.
    m_SceneRenderer->InvalidateOcclusionState();

    for (TUnique<RenderPass>& pass : m_Passes)
        pass->Resize(m_ViewportWidth, m_ViewportHeight);
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

void Renderer::Render(const Camera& camera, StreamingManager& streamingManager, const TArray<RenderInstance>& renderInstances,
                      const TArray<SceneLight>& lights)
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

    // The wait above is what makes this safe: everything last cycle's graph retired into this slot is
    // now provably finished on the GPU, so it can be released before this frame's graph refills it.
    m_RenderGraphAllocator->BeginFrame((uint32)m_FrameSlot);

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
    m_GPUScene.BuildLights(lights, (uint32)m_FrameSlot);
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

    // Persistent targets are re-imported every frame carrying their cross-frame GPU state; everything
    // intra-frame is a transient the graph owns outright (see DeclareFrameTargets).
    RGTextureHandle sceneColorHandle = graph.ImportTexture("Scene Color", m_SceneColorTexture, GetImportedState(m_SceneColorTexture.Get()));
    RGTextureHandle depthHandle = graph.ImportTexture("Scene Depth", m_DepthTexture, GetImportedState(m_DepthTexture.Get()));
    RGTextureHandle hzbHandle = graph.ImportTexture("Scene HZB", m_HZBTexture, GetImportedState(m_HZBTexture.Get()));
    RGTextureHandle backbufferHandle = graph.ImportTexture("Back Buffer", backBuffer, agfx::ResourceState::Present);

    m_FrameContext.ClearBlackboard();
    m_FrameContext.device = &m_Device;
    m_FrameContext.gpuScene = &m_GPUScene;
    m_FrameContext.schemes = &m_SchemeRegistry;
    m_FrameContext.camera = &camera;
    m_FrameContext.renderInstances = &renderInstances;
    m_FrameContext.lights = &lights;
    m_FrameContext.width = m_ViewportWidth;
    m_FrameContext.height = m_ViewportHeight;
    m_FrameContext.windowWidth = (uint32)width;
    m_FrameContext.windowHeight = (uint32)height;
    m_FrameContext.frameIndex = (uint32)m_FrameSlot;
    m_FrameContext.hzbResources = &m_HZB;
    m_FrameContext.sceneColor = sceneColorHandle;
    m_FrameContext.depth = depthHandle;
    m_FrameContext.hzb = hzbHandle;
    m_FrameContext.backBuffer = backbufferHandle;

    DeclareFrameTargets(graph, m_FrameContext);

    // Anything a pass must do before the graph exists -- uploading camera constants, for one, which
    // both cull dispatches read while the graph is still being built.
    for (TUnique<RenderPass>& pass : m_Passes) {
        if (pass->Enabled(m_FrameContext))
            pass->BeginFrame(m_FrameContext);
    }

    // The async-compute queue is caller-owned by design (see RenderGraph::SetQueueCommandBuffer), so
    // its command buffer has to be live before any pass targeting it registers.
    agfx::CommandBuffer* computeCommandBuffer = nullptr;
    agfx::QueryPool* computeQueryPool = nullptr;
    if (m_AccelStructManager->ShouldBuild()) {
        m_AccelStructManager->WaitForFrameSlot(m_FrameSlot);
        computeCommandBuffer = &m_AccelStructManager->GetFrameCommandBuffer(m_FrameSlot);
        computeQueryPool = &m_AccelStructManager->GetTimingQueryPool(m_FrameSlot);
        computeCommandBuffer->Reset();
        computeCommandBuffer->Begin();
        graph.SetQueueCommandBuffer(RGQueue::Compute, computeCommandBuffer);
    }

    // The whole frame. Order here is execution order -- the graph derives barriers but never reorders.
    for (TUnique<RenderPass>& pass : m_Passes) {
        if (pass->Enabled(m_FrameContext))
            pass->Register(graph, m_FrameContext);
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

    // Imported resources only -- transients do not outlive the graph, so they have no state to carry.
    SetImportedState(m_SceneColorTexture.Get(), graph.GetFinalState(sceneColorHandle));
    SetImportedState(m_DepthTexture.Get(), graph.GetFinalState(depthHandle));
    SetImportedState(m_HZBTexture.Get(), graph.GetFinalState(hzbHandle));
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
