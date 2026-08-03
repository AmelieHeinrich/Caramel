/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:57:20
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Application.hpp"

#include <Caramel/Core/Logger.hpp>
#include <Caramel/Core/Input.hpp>
#include <Caramel/Core/JobSystem.hpp>
#include <Caramel/Asset/Model.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>

Application::Application(const ApplicationInfo& info)
    : m_Info(info)
{
    Logger::Initialize();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        CARAMEL_ERROR("SDL_Init failed: {}", SDL_GetError());
        assert(false);
    }
    Input::Initialize();

    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;
    String apiName = "None";
    if (info.Maximized)
        windowFlags |= SDL_WINDOW_MAXIMIZED;
    switch (GetCurrentPlatform()) {
        case EPlatform::kMac:
            windowFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_METAL;
            apiName = "Metal 4";
            break;
        case EPlatform::kLinux:
            windowFlags |= SDL_WINDOW_VULKAN;
            apiName = "Vulkan";
            break;
        case EPlatform::kWindows:
            apiName = "DirectX 12";
            break;
        default:
            CARAMEL_ERROR("Unsupported platform for window creation");
            assert(false);
    }

    m_Window = SDL_CreateWindow(("Caramel | " + apiName).c_str(), info.Width, info.Height, windowFlags);
    assert(m_Window != nullptr && "Failed to create SDL window");

    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForOther(m_Window);

    m_Renderer = MakeUnique<Renderer>(m_Window);
    m_DeviceInfo = m_Renderer->GetDeviceInfo();

    JobSystem::Initialize();
    m_StreamingManager = MakeUnique<StreamingManager>();
    m_StreamingManager->Init(m_Renderer->GetDevice());
}

Application::~Application()
{
    JobSystem::Get().WaitAll();
    JobSystem::Shutdown();

    m_StreamingManager.reset();
    m_Renderer.reset();

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    Input::Shutdown();
    SDL_DestroyWindow(m_Window);
    SDL_Quit();
}

void Application::Run()
{
    while (m_Running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            Input::ProcessEvent(event);
            if (event.type == SDL_EVENT_QUIT) {
                m_Running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                m_Renderer->Resize();
            }
        }

        m_StreamingManager->Update();

        ImGui_ImplSDL3_NewFrame();
        Input::NewFrame();
        ImGui::NewFrame();
        ShowOverlay();
        ShowContentViewer();
        ImGui::Render();

        m_Renderer->Render();
    }
}

void Application::ShowOverlay()
{
    ImGui::SetNextWindowPos(ImVec2(30, 20));
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
    ImGui::Separator();
    ImGui::Text("Device: %s", m_DeviceInfo.name);
    ImGui::Text("Driver: %s", m_DeviceInfo.driverVersion);
    ImGui::Text("Raytracing: %s - Mesh Shaders: %s", m_DeviceInfo.supportsRayTracing ? "Yes" : "No", m_DeviceInfo.supportsMeshShaders ? "Yes" : "No");
    ImGui::End();
}

void Application::ShowContentViewer()
{
    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    ImGui::Begin("Content Viewer");

    if (!m_ContentViewerLoaded) {
        if (ImGui::Button("Load Sponza Textures")) {
            m_StreamingManager->LoadDirectory("Content/Cache/Sponza/Textures");
            m_ContentViewerLoaded = true;
        }
    } else {
        ImGui::Text("%d textures loaded", (int)m_StreamingManager->GetTextures().Size());
    }

    ImGui::Separator();

    ImGui::Checkbox("Automatic streaming", &m_ContentViewerAutoStream);
    if (m_ContentViewerAutoStream) {
        ImGui::SliderFloat("Interval (s)", &m_ContentViewerStreamInterval, 0.1f, 2.0f);

        m_ContentViewerStreamTimer += ImGui::GetIO().DeltaTime;
        if (m_ContentViewerStreamTimer >= m_ContentViewerStreamInterval) {
            m_ContentViewerStreamTimer = 0.0f;
            for (const TShared<StreamingTexture>& texture : m_StreamingManager->GetTextures())
                texture->RequestNextMip(*m_StreamingManager);
        }
    } else if (ImGui::Button("Advance All")) {
        for (const TShared<StreamingTexture>& texture : m_StreamingManager->GetTextures())
            texture->RequestNextMip(*m_StreamingManager);
    }

    ImGui::Separator();

    const float thumbSize = 128.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(availWidth / (thumbSize + 8.0f));
    if (columnCount < 1)
        columnCount = 1;

    int index = 0;
    for (const TShared<StreamingTexture>& texture : m_StreamingManager->GetTextures()) {
        ImGui::BeginGroup();
        uint32 residentMip = texture->SnapshotResidentMip();
        if (residentMip != StreamingTexture::kNoResidentMip) {
            ImGui::Image(texture->GetDisplayTexID(), ImVec2(thumbSize, thumbSize));
            ImGui::Text("%ux%u", texture->GetWidth(), texture->GetHeight());
            ImGui::Text("mip %u/%u", residentMip, texture->GetMipCount() - 1);
        } else {
            ImGui::Dummy(ImVec2(thumbSize, thumbSize));
            ImGui::Text("Loading...");
        }
        ImGui::EndGroup();

        index++;
        if (index % columnCount != 0)
            ImGui::SameLine();
    }

    ImGui::End();
}
