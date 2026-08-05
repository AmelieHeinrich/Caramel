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
#include <Caramel/Renderer/AccelerationStructureManager.hpp>

#include <AGFX/agfx.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <FontAwesome/FA.h>

#include <cstdio>
#include <cstring>

namespace
{
    bool BeginStatBlock(const char* id)
    {
        return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit);
    }

    void HumanCount(uint64 value, char out[32])
    {
        double scaled = 0.0;
        const char* suffix = nullptr;

        if (value >= 1000000000ull) { scaled = value / 1e9; suffix = " billion"; }
        else if (value >= 1000000ull) { scaled = value / 1e6; suffix = " million"; }
        else if (value >= 10000ull) { scaled = value / 1e3; suffix = "k"; }
        else { snprintf(out, 32, "%llu", (unsigned long long)value); return; }

        snprintf(out, 32, "%.2f", scaled);
        char* end = out + strlen(out) - 1;
        while (*end == '0') *end-- = '\0';
        if (*end == '.') *end = '\0';
        strcat(out, suffix);
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

    if (deviceInfo.supportsRayTracing) {
        const AccelerationStructureManager& accel = Renderer::Get().GetAccelStructManager();
        EditorTheme::SectionHeader("Acceleration Structures");
        if (BeginStatBlock("OverlayAccelStruct")) {
            EditorTheme::StatRow("BLAS built", "%d / %d", (int)accel.GetReadyBLASCount(), (int)accel.GetTrackedModelCount());
            EditorTheme::StatRow("TLAS instances", "%d", (int)accel.GetLastTLASInstanceCount());
            ImGui::EndTable();
        }
    }

    EditorTheme::SectionHeader("Frame");
    if (BeginStatBlock("OverlayFrame")) {
        // CPU is the wall-clock time of the last full Application::Run() iteration (ImGui's own
        // per-frame delta, includes any GPU fence wait) -- GPU is the graphics-queue render graph
        // total from RenderGraph's timestamp queries, see Renderer::GetLastGpuFrameTimeMs.
        EditorTheme::StatRow("CPU Frame", "%.2f ms", ImGui::GetIO().DeltaTime * 1000.0f);
        EditorTheme::StatRow("GPU Frame", "%.2f ms", Renderer::Get().GetLastGpuFrameTimeMs());

        EditorTheme::StatRow("Viewport", "%dx%d", (int)context.ViewportRectSize.x, (int)context.ViewportRectSize.y);

        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSizeInPixels(window, &windowWidth, &windowHeight);
        if (windowWidth != (int)context.ViewportRectSize.x || windowHeight != (int)context.ViewportRectSize.y)
            EditorTheme::StatRow("Window", "%dx%d", windowWidth, windowHeight);

        // Scheme buckets == pipeline switches per frame. Material batches change no GPU state yet;
        // they are shown because they are the unit that becomes one indirect bundle region later.
        const GPUScene& gpuScene = Renderer::Get().GetGPUScene();
        char count[32];
        HumanCount(gpuScene.GetInstanceCount(), count);
        EditorTheme::StatRow("Pre-cull instances", "%s", count);
        HumanCount(gpuScene.GetPreCullMeshletCount(), count);
        EditorTheme::StatRow("Pre-cull meshlets", "%s", count);
        HumanCount(gpuScene.GetPreCullTriangleCount(), count);
        EditorTheme::StatRow("Pre-cull triangles", "%s", count);
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
