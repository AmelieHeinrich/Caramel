/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:02:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "OverlayPanel.hpp"

#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Renderer/Renderer.hpp>

#include <AGFX/agfx.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <FontAwesome/FA.h>

void OverlayPanel::Draw(EditorContext& context, StreamingManager& streaming, SDL_Window* window, const agfxDeviceInfo& deviceInfo, bool& showColliders)
{
    ImGui::SetNextWindowPos(ImVec2(context.ViewportRectMin.x + 12.0f, context.ViewportRectMin.y + 12.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Separator();
    ImGui::Text("Caramel : a modern renderer built by Amélie Heinrich");
#if defined(CARAMEL_WINDOWS)
    ImGui::Text("Backend: D3D12");
#elif defined(CARAMEL_LINUX)
    ImGui::Text("Backend: Vulkan 1.4");
#elif defined(CARAMEL_MAC)
    ImGui::Text("Backend: Metal 4");
#endif
    ImGui::Text("Viewport: %dx%d", (int)context.ViewportRectSize.x, (int)context.ViewportRectSize.y);
    {
        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSizeInPixels(window, &windowWidth, &windowHeight);
        if (windowWidth != (int)context.ViewportRectSize.x || windowHeight != (int)context.ViewportRectSize.y)
            ImGui::Text("Window: %dx%d", windowWidth, windowHeight);
    }
    ImGui::Separator();
    ImGui::Text("Device: %s", deviceInfo.name);
    ImGui::Text("Driver: %s", deviceInfo.driverVersion);
    ImGui::Text("Raytracing: %s - Mesh Shaders: %s", deviceInfo.supportsRayTracing ? "Yes" : "No", deviceInfo.supportsMeshShaders ? "Yes" : "No");

    ImGui::Separator();
    ImGui::Text("%d meshes, %d textures streamed", (int)streaming.GetModels().Size(), (int)streaming.GetTextures().Size());

    {
        // Scheme buckets == pipeline switches per frame. Material batches change no GPU state yet;
        // they are shown because they are the unit that becomes one indirect bundle region later.
        const GPUScene& gpuScene = Renderer::Get().GetGPUScene();
        ImGui::Text("%d draws in %d material batches, %d scheme buckets",
                    (int)gpuScene.GetDraws().Size(), (int)gpuScene.GetBatches().Size(), (int)gpuScene.GetBuckets().Size());
    }

    bool autoStream = streaming.GetAutoStream();
    if (ImGui::Checkbox("Automatic streaming", &autoStream))
        streaming.SetAutoStream(autoStream);

    if (!autoStream && ImGui::Button("Advance"))
        streaming.PumpStreaming();

    ImGui::Checkbox(ICON_FA_CUBE " Show colliders", &showColliders);

    ImGui::Text("Upload budget: %.1f / %.1f MB in flight",
                streaming.GetBytesInFlight() / (1024.0 * 1024.0),
                StreamingManager::GetMaxBytesInFlight() / (1024.0 * 1024.0));

    ImGui::End();
}
