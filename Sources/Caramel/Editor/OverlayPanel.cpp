/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:02:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "OverlayPanel.hpp"

#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Editor/EditorTheme.hpp>
#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Renderer/Renderer.hpp>

#include <AGFX/agfx.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <FontAwesome/FA.h>

namespace
{
    bool BeginStatBlock(const char* id)
    {
        return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit);
    }
}

void OverlayPanel::Draw(EditorContext& context, StreamingManager& streaming, SDL_Window* window, const agfxDeviceInfo& deviceInfo)
{
    float margin = EditorTheme::Em(0.75f);
    ImGui::SetNextWindowPos(ImVec2(context.ViewportRectMin.x + margin, context.ViewportRectMin.y + margin));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

    // Framerate reads as the headline number rather than one row among a dozen.
    if (ImFont* header = EditorTheme::HeaderFont())
        ImGui::PushFont(header, ImGui::GetStyle().FontSizeBase * 1.35f);
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    if (EditorTheme::HeaderFont())
        ImGui::PopFont();

    ImGui::TextDisabled("Caramel -- a modern renderer by Amélie Heinrich");

    EditorTheme::SectionHeader("Device");
    if (BeginStatBlock("OverlayDevice")) {
#if defined(CARAMEL_WINDOWS)
        EditorTheme::StatRow("Backend", "D3D12");
#elif defined(CARAMEL_LINUX)
        EditorTheme::StatRow("Backend", "Vulkan 1.4");
#elif defined(CARAMEL_MACOS)
        EditorTheme::StatRow("Backend", "Metal 4");
#endif
        EditorTheme::StatRow("Device", "%s", deviceInfo.name);
        EditorTheme::StatRow("Driver", "%s", deviceInfo.driverVersion);
        EditorTheme::StatRow("Raytracing", "%s", deviceInfo.supportsRayTracing ? "Yes" : "No");
        EditorTheme::StatRow("Mesh shaders", "%s", deviceInfo.supportsMeshShaders ? "Yes" : "No");
        ImGui::EndTable();
    }

    EditorTheme::SectionHeader("Frame");
    if (BeginStatBlock("OverlayFrame")) {
        EditorTheme::StatRow("Viewport", "%dx%d", (int)context.ViewportRectSize.x, (int)context.ViewportRectSize.y);

        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSizeInPixels(window, &windowWidth, &windowHeight);
        if (windowWidth != (int)context.ViewportRectSize.x || windowHeight != (int)context.ViewportRectSize.y)
            EditorTheme::StatRow("Window", "%dx%d", windowWidth, windowHeight);

        // Scheme buckets == pipeline switches per frame. Material batches change no GPU state yet;
        // they are shown because they are the unit that becomes one indirect bundle region later.
        const GPUScene& gpuScene = Renderer::Get().GetGPUScene();
        EditorTheme::StatRow("Draws", "%d", (int)gpuScene.GetDraws().Size());
        EditorTheme::StatRow("Material batches", "%d", (int)gpuScene.GetBatches().Size());
        EditorTheme::StatRow("Scheme buckets", "%d", (int)gpuScene.GetBuckets().Size());
        ImGui::EndTable();
    }

    EditorTheme::SectionHeader("Streaming");
    if (BeginStatBlock("OverlayStreaming")) {
        EditorTheme::StatRow("Meshes", "%d", (int)streaming.GetModels().Size());
        EditorTheme::StatRow("Textures", "%d", (int)streaming.GetTextures().Size());
        EditorTheme::StatRow("Upload budget", "%.1f / %.1f MB",
                             streaming.GetBytesInFlight() / (1024.0 * 1024.0),
                             StreamingManager::GetMaxBytesInFlight() / (1024.0 * 1024.0));
        if (!streaming.GetAutoStream())
            EditorTheme::StatRow("Mode", ICON_FA_HAND " Manual");
        ImGui::EndTable();
    }

    ImGui::End();
}
