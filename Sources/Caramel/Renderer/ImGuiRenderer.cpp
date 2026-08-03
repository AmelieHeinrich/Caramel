/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:30:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ImGuiRenderer.hpp"

#include <Caramel/Renderer/Shader/ShaderServer.hpp>
#include <FontAwesome/FA.h>

#include <cstring>
#include <utility>

namespace {

// Heap-allocated per-ImTextureData backend state (stored in ImTextureData::BackendUserData) --
// owns the live agfx::Texture/TextureView for one ImGui texture (the font atlas, or a
// user-registered texture).
struct ImGuiTextureData
{
    agfx::Texture Texture;
    agfx::TextureView View;
};

constexpr const char* kImGuiShaderPath = "Content/Shaders/ImGui.hlsl";

// "Caramel Noir" theme -- warm dark charcoal base with a single caramel-amber accent, in the
// spirit of moody, high-contrast game UIs (Persona 5's overlines, Death Stranding/Super Metroid's
// amber HUD glows, Fear & Hunger 2's grim warm palette) kept subtle rather than loud.
ImVec4 Col(uint8 r, uint8 g, uint8 b, uint8 a = 255)
{
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

void ApplyCaramelTheme(ImGuiStyle& style)
{
    const ImVec4 bgDarkest  = Col(0x15, 0x12, 0x0F);
    const ImVec4 bgDark     = Col(0x1C, 0x18, 0x15);
    const ImVec4 bgPanel    = Col(0x24, 0x1F, 0x1A);
    const ImVec4 bgHover    = Col(0x2E, 0x27, 0x21);
    const ImVec4 bgActive   = Col(0x3A, 0x2E, 0x22);
    const ImVec4 grab       = Col(0x3A, 0x32, 0x2A);
    const ImVec4 grabHover  = Col(0x4A, 0x40, 0x38);
    const ImVec4 border     = Col(0x33, 0x2C, 0x24, 0x80);
    const ImVec4 text       = Col(0xEC, 0xE4, 0xD8);
    const ImVec4 textDim    = Col(0x7A, 0x6F, 0x60);
    const ImVec4 accent       = Col(0xD9, 0x8B, 0x39);
    const ImVec4 accentHover  = Col(0xE9, 0xA3, 0x4E);
    const ImVec4 accentActive = Col(0xB3, 0x6B, 0x25);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = text;
    colors[ImGuiCol_TextDisabled]          = textDim;
    colors[ImGuiCol_WindowBg]              = bgDarkest;
    colors[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg]               = bgDark;
    colors[ImGuiCol_Border]                = border;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg]               = bgPanel;
    colors[ImGuiCol_FrameBgHovered]        = bgHover;
    colors[ImGuiCol_FrameBgActive]         = bgActive;
    colors[ImGuiCol_TitleBg]               = bgDark;
    colors[ImGuiCol_TitleBgActive]         = bgDark;
    colors[ImGuiCol_TitleBgCollapsed]      = Col(0x15, 0x12, 0x0F, 0xC0);
    colors[ImGuiCol_MenuBarBg]             = bgDark;
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab]         = grab;
    colors[ImGuiCol_ScrollbarGrabHovered]  = grabHover;
    colors[ImGuiCol_ScrollbarGrabActive]   = accent;
    colors[ImGuiCol_CheckMark]             = accent;
    colors[ImGuiCol_SliderGrab]            = accent;
    colors[ImGuiCol_SliderGrabActive]      = accentHover;
    colors[ImGuiCol_Button]                = bgHover;
    colors[ImGuiCol_ButtonHovered]         = bgActive;
    colors[ImGuiCol_ButtonActive]          = accentActive;
    colors[ImGuiCol_Header]                = ImVec4(accent.x, accent.y, accent.z, 0.0f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(accent.x, accent.y, accent.z, 0.20f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    colors[ImGuiCol_Separator]             = border;
    colors[ImGuiCol_SeparatorHovered]      = accent;
    colors[ImGuiCol_SeparatorActive]       = accentHover;
    colors[ImGuiCol_ResizeGrip]            = ImVec4(accent.x, accent.y, accent.z, 0.0f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_ResizeGripActive]      = accent;
    colors[ImGuiCol_InputTextCursor]       = accent;
    colors[ImGuiCol_Tab]                   = bgDark;
    colors[ImGuiCol_TabHovered]            = ImVec4(accent.x, accent.y, accent.z, 0.20f);
    colors[ImGuiCol_TabSelected]           = bgPanel;
    colors[ImGuiCol_TabSelectedOverline]   = accent;
    colors[ImGuiCol_TabDimmed]             = bgDarkest;
    colors[ImGuiCol_TabDimmedSelected]     = bgDark;
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    colors[ImGuiCol_PlotLines]             = textDim;
    colors[ImGuiCol_PlotLinesHovered]      = accentHover;
    colors[ImGuiCol_PlotHistogram]         = accent;
    colors[ImGuiCol_PlotHistogramHovered]  = accentHover;
    colors[ImGuiCol_TableHeaderBg]         = bgPanel;
    colors[ImGuiCol_TableBorderStrong]     = border;
    colors[ImGuiCol_TableBorderLight]      = ImVec4(border.x, border.y, border.z, 0.30f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.02f);
    colors[ImGuiCol_TextLink]              = accentHover;
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_TreeLines]             = border;
    colors[ImGuiCol_DragDropTarget]        = accent;
    colors[ImGuiCol_DragDropTargetBg]      = ImVec4(accent.x, accent.y, accent.z, 0.15f);
    colors[ImGuiCol_UnsavedMarker]         = accent;
    colors[ImGuiCol_NavCursor]             = accent;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.50f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.60f);

    // Slightly rounded, compact -- small radii and tight padding rather than fully flat or plush.
    style.WindowRounding    = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildRounding     = 4.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupRounding     = 4.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameRounding     = 3.0f;
    style.FrameBorderSize   = 1.0f;
    style.ScrollbarRounding = 6.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabRounding      = 3.0f;
    style.GrabMinSize       = 10.0f;
    style.TabRounding       = 3.0f;
    style.TabBarBorderSize  = 1.0f;
    style.TabBarOverlineSize = 2.0f;
    style.WindowPadding      = ImVec2(10.0f, 8.0f);
    style.FramePadding       = ImVec2(8.0f, 4.0f);
    style.ItemSpacing        = ImVec2(6.0f, 5.0f);
    style.ItemInnerSpacing   = ImVec2(6.0f, 5.0f);
    style.CellPadding        = ImVec2(6.0f, 4.0f);
    style.IndentSpacing      = 16.0f;
    style.SeparatorTextBorderSize = 1.0f;
}

} // namespace

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

    // Ubuntu-Regular for body text (readable at compact UI sizes); Quicksand-Bold reserved as a
    // separate display font for section headers/titles via GetHeaderFont().
    io.FontDefault = io.Fonts->AddFontFromFileTTF("Content/Fonts/Ubuntu-Regular.ttf", 17.0f);

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

    // Barriers are command-buffer-level, so each upload gets its own short pass around the copy.
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
