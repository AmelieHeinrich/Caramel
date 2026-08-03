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

    // Drain the GPU before tearing anything down: in-flight frames and uploads still reference
    // the staging buffers, swapchain semaphores, pipelines and command buffers destroyed below.
    m_Renderer->GetDevice().WaitIdle();

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
        m_Camera.Update(ImGui::GetIO().DeltaTime);
        ImGui::NewFrame();
        ShowOverlay();
        ShowModelViewer();
        ImGui::Render();

        m_Renderer->Render(m_Camera, *m_StreamingManager);
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

void Application::ShowModelViewer()
{
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin("Model Viewer");

    if (!m_ModelViewerLoaded) {
        if (ImGui::Button("Load Bistro")) {
            m_StreamingManager->LoadModel("Content/Cache/IntelSponza/IntelSponza.cmdl");
            m_ModelViewerLoaded = true;
        }
    } else {
        ImGui::Text("%d meshes, %d textures loaded",
                    (int)m_StreamingManager->GetModels().Size(),
                    (int)m_StreamingManager->GetTextures().Size());
    }

    ImGui::Separator();

    bool autoStream = m_StreamingManager->GetAutoStream();
    if (ImGui::Checkbox("Automatic streaming##Model", &autoStream))
        m_StreamingManager->SetAutoStream(autoStream);

    if (!autoStream && ImGui::Button("Advance##Model"))
        m_StreamingManager->PumpStreaming();

    ImGui::Text("Upload budget: %.1f / %.1f MB in flight",
                m_StreamingManager->GetBytesInFlight() / (1024.0 * 1024.0),
                StreamingManager::GetMaxBytesInFlight() / (1024.0 * 1024.0));

    ImGui::Separator();

    if (ImGui::BeginTable("ModelStreamingTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Mesh");
        ImGui::TableSetupColumn("Resident LOD");
        ImGui::TableSetupColumn("Meshlets");
        ImGui::TableHeadersRow();

        for (const TShared<StreamingModel>& model : m_StreamingManager->GetModels()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(model->GetMesh().name.CStr());

            ImGui::TableSetColumnIndex(1);
            uint32 residentLOD = model->SnapshotResidentLOD();
            if (residentLOD != StreamingModel::kNoResidentLOD)
                ImGui::Text("%u/%u", residentLOD, (uint32)CaramelAsset::kLodCount - 1);
            else
                ImGui::TextUnformatted("Loading...");

            ImGui::TableSetColumnIndex(2);
            if (residentLOD != StreamingModel::kNoResidentLOD)
                ImGui::Text("%u", model->GetMeshletCount(residentLOD));
            else
                ImGui::TextUnformatted("-");
        }

        ImGui::EndTable();
    }

    ImGui::End();
}
