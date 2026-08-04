/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:30:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ImGuiRenderer.hpp"
#include "imgui.h"

#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <FontAwesome/FA.h>

#include <cstring>
#include <utility>

namespace {

struct ImGuiTextureData
{
    agfx::Texture Texture;
    agfx::TextureView View;
};

constexpr const char* kImGuiShaderPath = "Content/Shaders/ImGui.hlsl";

ImVec4 Col(uint8 r, uint8 g, uint8 b, uint8 a = 255)
{
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

void ApplyCaramelTheme(ImGuiStyle& style)
{
    ImVec4* colors = style.Colors;

    // --- 1. Sizing and Spacing (Sharp & Technical) ---
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(5.0f, 3.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    // --- 2. Borders & Rounding (Low rounding for a technical look) ---
    style.WindowRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    // --- 3. Color Palette ---

    // Text
    colors[ImGuiCol_Text] = ImVec4(1.00f, 0.95f, 0.80f, 1.00f); // Soft cream-yellow
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.45f, 0.30f, 1.00f);

    // Backgrounds
    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.06f, 1.00f); // Near black
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.06f, 0.96f);

    // Borders
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.25f, 0.10f, 0.80f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frames (Inputs, Checkboxes, etc.)
    colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.22f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.30f, 0.15f, 1.00f);

    // Title Bars
    colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.11f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.18f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.05f, 0.04f, 1.00f);

    // Menus
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.11f, 0.08f, 1.00f);

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.04f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.30f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.40f, 0.15f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.50f, 0.20f, 1.00f);

    // Interactables (The High-Vis Amber)
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.70f, 0.60f, 0.10f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.30f, 0.25f, 0.05f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.30f, 0.25f, 0.05f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.35f, 0.30f, 0.10f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.08f, 0.07f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);

    // Tables
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.18f, 0.16f, 0.10f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.35f, 0.30f, 0.15f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.25f, 0.20f, 0.10f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    // Misc
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.95f, 0.80f, 0.10f, 0.25f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 0.85f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);

#ifdef IMGUI_HAS_DOCK
    colors[ImGuiCol_DockingPreview] = ImVec4(0.95f, 0.80f, 0.10f, 0.40f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);
#endif
}

}

ImGuiRenderer::ImGuiRenderer(agfx::Device& device, agfx::CommandQueue& queue, agfx::TextureFormat colorFormat, uint32 framesInFlight)
    : m_Device(&device)
    , m_Queue(&queue)
{
    agfx::RenderPipelineCreateInfo pipelineInfo;
    pipelineInfo.SetName("ImGui Pipeline")
                .SetCullMode(agfx::CullMode::None)
                .SetFrontFace(agfx::FrontFace::CounterClockwise)
                .SetTopology(agfx::Topology::Triangles)
                .SetDepthState(false, false)
                .AddColorAttachment(colorFormat)
                .SetAlphaBlend();
    ShaderServer::RegisterRenderPipeline(pipelineInfo, kImGuiShaderPath);

    agfx::SamplerCreateInfo samplerInfo;
    samplerInfo.SetFilter(agfx::SamplerFilter::Linear)
               .SetAddressMode(agfx::AddressMode::ClampToEdge)
               .SetComparisonFunction(agfx::ComparisonFunction::Always)
               .SetLodRange(0.0f, 0.0f);
    m_Sampler = m_Device->CreateSampler(samplerInfo);

    m_VertexBuffers.Resize(framesInFlight);
    m_VertexBufferViews.Resize(framesInFlight);
    m_VertexBufferCapacity.Resize(framesInFlight, 0);
    m_IndexBuffers.Resize(framesInFlight);
    m_IndexBufferCapacity.Resize(framesInFlight, 0);

    m_UploadCmdBuffer = m_Device->CreateCommandBuffer(*m_Queue);
    m_UploadFence = m_Device->CreateFence();
    m_UploadCmdBuffer.Reset();
    m_UploadCmdBuffer.Begin();

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "caramel_imgui_renderer";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    io.FontDefault = io.Fonts->AddFontFromFileTTF("Content/Fonts/Ubuntu-Regular.ttf", 17.0f);
#if defined(CARAMEL_MACOS)
    io.FontGlobalScale = 0.75f;
#endif

    static const ImWchar kIconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF("Content/Fonts/FA.ttf", 17.0f, &iconConfig, kIconRanges);

    m_HeaderFont = io.Fonts->AddFontFromFileTTF("Content/Fonts/Quicksand-Bold.ttf", 20.0f);

    ApplyCaramelTheme(ImGui::GetStyle());
}

ImGuiRenderer::~ImGuiRenderer()
{
    ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    for (ImTextureData* tex : platformIO.Textures)
        DestroyTexture(tex);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

void ImGuiRenderer::UploadTexture(agfx::Texture& dstTexture, const agfxTextureRegion& region, const void* data, uint32 dataSize, uint32 bytesPerRow, uint32 bytesPerImage, agfx::ResourceState oldState)
{
    if (dataSize == 0)
        return;

    agfx::BufferCreateInfo stagingInfo;
    stagingInfo.SetSize(dataSize).SetStride(dataSize).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
    agfx::Buffer staging = m_Device->CreateBuffer(stagingInfo);
    staging.SetName("ImGui Upload Staging Buffer");

    {
        agfx::MappedBuffer mapped(staging);
        memcpy(mapped.Get(), data, dataSize);
    }

    m_UploadCmdBuffer.TextureBarrier(dstTexture, oldState, agfx::ResourceState::CopyDest);
    agfx::ComputePass pass = m_UploadCmdBuffer.BeginComputePass("ImGui Texture Upload");
    pass.CopyBufferToTexture(staging, 0, dstTexture, region, 0, 0, bytesPerRow, bytesPerImage);
    pass.End();
    m_UploadCmdBuffer.TextureBarrier(dstTexture, agfx::ResourceState::CopyDest, agfx::ResourceState::PixelShaderResource);

    m_PendingStagingBuffers.PushBack(std::move(staging));
}

void ImGuiRenderer::FlushUploads()
{
    if (m_PendingStagingBuffers.IsEmpty())
        return;

    m_Device->MakeResourcesResident();

    m_UploadCmdBuffer.End();
    m_Queue->Submit(m_UploadCmdBuffer);

    m_UploadFenceValue++;
    m_Queue->Signal(m_UploadFence, m_UploadFenceValue);
    m_UploadFence.Wait(m_UploadFenceValue);

    m_PendingStagingBuffers.Clear();

    m_UploadCmdBuffer.Reset();
    m_UploadCmdBuffer.Begin();
}

void ImGuiRenderer::UpdateTexture(ImTextureData* tex)
{
    if (tex->Status == ImTextureStatus_WantCreate) {
        agfx::TextureCreateInfo textureInfo;
        textureInfo.SetType(agfx::TextureType::Texture2D)
                   .SetFormat(agfx::TextureFormat::RGBA8Unorm)
                   .SetUsage(agfx::TextureUsage::Sampled)
                   .SetSize((uint32)tex->Width, (uint32)tex->Height, 1)
                   .SetMipLevels(1);

        auto* backendTex = new ImGuiTextureData();
        backendTex->Texture = m_Device->CreateTexture(textureInfo);
        backendTex->Texture.SetName("ImGui Font Texture");

        agfxTextureRegion region{};
        region.x = 0;
        region.y = 0;
        region.z = 0;
        region.width = (uint32)tex->Width;
        region.height = (uint32)tex->Height;
        region.depth = 1;

        uint32 bytesPerRow = (uint32)tex->Width * 4;
        uint32 dataSize = bytesPerRow * (uint32)tex->Height;
        UploadTexture(backendTex->Texture, region, tex->Pixels, dataSize, bytesPerRow, dataSize, agfx::ResourceState::Common);

        agfx::TextureViewCreateInfo viewInfo;
        viewInfo.SetTexture(backendTex->Texture.Get())
                .SetFormat(agfx::TextureFormat::RGBA8Unorm)
                .SetType(agfx::TextureType::Texture2D)
                .SetMipRange(0, 1)
                .SetArrayRange(0, 1)
                .SetWriteable(false);
        backendTex->View = m_Device->CreateTextureView(viewInfo);

        tex->SetTexID((ImTextureID)(intptr_t)backendTex->View.GetHandle());
        tex->SetStatus(ImTextureStatus_OK);
        tex->BackendUserData = backendTex;
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        auto* backendTex = (ImGuiTextureData*)tex->BackendUserData;

        for (ImTextureRect& r : tex->Updates) {
            TArray<uint8> packed((size_t)r.w * r.h * 4);
            for (int row = 0; row < (int)r.h; ++row) {
                const void* src = tex->GetPixelsAt(r.x, r.y + row);
                memcpy(packed.Data() + (size_t)row * r.w * 4, src, (size_t)r.w * 4);
            }

            agfxTextureRegion region{};
            region.x = (uint32)r.x;
            region.y = (uint32)r.y;
            region.z = 0;
            region.width = (uint32)r.w;
            region.height = (uint32)r.h;
            region.depth = 1;

            uint32 bytesPerRow = (uint32)r.w * 4;
            uint32 dataSize = (uint32)packed.Size();
            UploadTexture(backendTex->Texture, region, packed.Data(), dataSize, bytesPerRow, dataSize, agfx::ResourceState::PixelShaderResource);
        }
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        DestroyTexture(tex);
    }
}

void ImGuiRenderer::DestroyTexture(ImTextureData* tex)
{
    if (auto* backendTex = (ImGuiTextureData*)tex->BackendUserData) {
        delete backendTex;
        tex->BackendUserData = nullptr;
    }
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void ImGuiRenderer::UpdateTextures(ImDrawData* drawData)
{
    if (drawData->Textures != nullptr) {
        for (ImTextureData* tex : *drawData->Textures) {
            if (tex->Status != ImTextureStatus_OK)
                UpdateTexture(tex);
        }
    }
    FlushUploads();
}

void ImGuiRenderer::RenderDrawData(ImDrawData* drawData, agfx::RenderPass& renderPass, uint32 fbWidth, uint32 fbHeight, uint32 frameIndex)
{
    if (!drawData || drawData->CmdListsCount == 0 || fbWidth == 0 || fbHeight == 0)
        return;

    UpdateTextures(drawData);

    uint64 vtxSize = (uint64)drawData->TotalVtxCount * sizeof(ImDrawVert);
    uint64 idxSize = (uint64)drawData->TotalIdxCount * sizeof(ImDrawIdx);
    if (vtxSize == 0 || idxSize == 0)
        return;

    if (vtxSize > m_VertexBufferCapacity[frameIndex]) {
        uint64 newCapacity = vtxSize + vtxSize / 2 + (uint64)sizeof(ImDrawVert) * 1024;

        agfx::BufferCreateInfo bufferInfo;
        bufferInfo.SetSize(newCapacity).SetStride(sizeof(ImDrawVert)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        m_VertexBuffers[frameIndex] = m_Device->CreateBuffer(bufferInfo);
        m_VertexBuffers[frameIndex].SetName("ImGui Vertex Buffer");

        agfx::BufferViewCreateInfo viewInfo;
        viewInfo.SetBuffer(m_VertexBuffers[frameIndex].Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(false);
        m_VertexBufferViews[frameIndex] = m_Device->CreateBufferView(viewInfo);

        m_VertexBufferCapacity[frameIndex] = newCapacity;
    }

    if (idxSize > m_IndexBufferCapacity[frameIndex]) {
        uint64 newCapacity = idxSize + idxSize / 2 + (uint64)sizeof(ImDrawIdx) * 1024;
        newCapacity = (newCapacity + sizeof(ImDrawIdx) - 1) & ~(uint64)(sizeof(ImDrawIdx) - 1);

        agfx::BufferCreateInfo bufferInfo;
        bufferInfo.SetSize(newCapacity).SetStride(sizeof(ImDrawIdx)).SetUsage(agfx::BufferUsage::Index).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        m_IndexBuffers[frameIndex] = m_Device->CreateBuffer(bufferInfo);
        m_IndexBuffers[frameIndex].SetName("ImGui Index Buffer");

        m_IndexBufferCapacity[frameIndex] = newCapacity;
    }

    {
        agfx::MappedBuffer vtxMapped(m_VertexBuffers[frameIndex]);
        agfx::MappedBuffer idxMapped(m_IndexBuffers[frameIndex]);
        uint8* vtxDst = vtxMapped.As<uint8>();
        uint8* idxDst = idxMapped.As<uint8>();
        uint64 vtxByteOffset = 0;
        uint64 idxByteOffset = 0;
        for (int i = 0; i < drawData->CmdListsCount; ++i) {
            const ImDrawList* list = drawData->CmdLists[i];
            uint64 vtxBytes = (uint64)list->VtxBuffer.Size * sizeof(ImDrawVert);
            uint64 idxBytes = (uint64)list->IdxBuffer.Size * sizeof(ImDrawIdx);
            if (vtxBytes > 0) memcpy(vtxDst + vtxByteOffset, list->VtxBuffer.Data, vtxBytes);
            if (idxBytes > 0) memcpy(idxDst + idxByteOffset, list->IdxBuffer.Data, idxBytes);
            vtxByteOffset += vtxBytes;
            idxByteOffset += idxBytes;
        }
    }

    m_Device->MakeResourcesResident();

    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(kImGuiShaderPath, {});
    if (!pipeline)
        return;

    renderPass.SetViewport(0.0f, 0.0f, (float)fbWidth, (float)fbHeight);
    renderPass.SetPipeline(*pipeline);

    ImVec2 clipOff = drawData->DisplayPos;
    ImVec2 clipScale = drawData->FramebufferScale;

    struct ImGuiPushConstants
    {
        float Scale[2];
        float Translate[2];
        uint32 VertexOffset;
        uint32 VertexBuffer;
        uint32 Texture;
        uint32 TextureSampler;
    } pc{};
    pc.Scale[0] = 2.0f / drawData->DisplaySize.x;
    pc.Scale[1] = -2.0f / drawData->DisplaySize.y;
    pc.Translate[0] = -1.0f - drawData->DisplayPos.x * pc.Scale[0];
    pc.Translate[1] = 1.0f - drawData->DisplayPos.y * pc.Scale[1];
    pc.VertexBuffer = (uint32)m_VertexBufferViews[frameIndex].GetHandle();
    pc.TextureSampler = (uint32)m_Sampler.GetHandle();

    int32 globalVtxOffset = 0;
    int32 globalIdxOffset = 0;
    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList* list = drawData->CmdLists[i];
        for (int cmdIndex = 0; cmdIndex < list->CmdBuffer.Size; ++cmdIndex) {
            const ImDrawCmd& cmd = list->CmdBuffer[cmdIndex];
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState)
                    cmd.UserCallback(list, &cmd);
                continue;
            }

            ImVec2 clipMin((cmd.ClipRect.x - clipOff.x) * clipScale.x, (cmd.ClipRect.y - clipOff.y) * clipScale.y);
            ImVec2 clipMax((cmd.ClipRect.z - clipOff.x) * clipScale.x, (cmd.ClipRect.w - clipOff.y) * clipScale.y);
            if (clipMin.x < 0.0f) clipMin.x = 0.0f;
            if (clipMin.y < 0.0f) clipMin.y = 0.0f;
            if (clipMax.x > (float)fbWidth) clipMax.x = (float)fbWidth;
            if (clipMax.y > (float)fbHeight) clipMax.y = (float)fbHeight;
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
                continue;
            if (cmd.ElemCount == 0)
                continue;

            renderPass.SetScissor((uint32)clipMin.x, (uint32)clipMin.y, (uint32)(clipMax.x - clipMin.x), (uint32)(clipMax.y - clipMin.y));

            pc.Texture = (uint32)(intptr_t)cmd.GetTexID();
            pc.VertexOffset = (uint32)(cmd.VtxOffset + globalVtxOffset);
            renderPass.PushConstants(pc);

            renderPass.DrawIndexed(m_IndexBuffers[frameIndex], cmd.ElemCount, 1, cmd.IdxOffset + globalIdxOffset, 0, 0);
        }
        globalVtxOffset += list->VtxBuffer.Size;
        globalIdxOffset += list->IdxBuffer.Size;
    }
}
