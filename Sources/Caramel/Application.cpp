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
#include <Caramel/Physics/Physics.hpp>
#include <Caramel/Physics/JoltMath.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Color.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>
#include <Jolt/Geometry/AABox.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <FontAwesome/FA.h>

#include <nlohmann/json.hpp>

#include <cfloat>
#include <filesystem>
#include <fstream>

Application::Application(const ApplicationInfo& info)
    : m_Info(info)
    , m_EditorContext{ m_Scene }
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
    m_EditorContext.Window = m_Window;
    m_EditorContext.Owner = this;

    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplSDL3_InitForOther(m_Window);

    m_Renderer = MakeUnique<Renderer>(m_Window, info.VSync);
    m_DeviceInfo = m_Renderer->GetDeviceInfo();

    JobSystem::Initialize();
    Physics::Initialize();
    m_JoltDebugRenderer = MakeUnique<JoltDebugRenderer>();

    m_StreamingManager = MakeUnique<StreamingManager>();
    m_StreamingManager->Init(m_Renderer->GetDevice());

    m_ScriptEngine = MakeUnique<ScriptEngine>();
    m_ScriptEngine->SetScriptDirectory("Content/Scripts");
    m_ScriptSystem = MakeUnique<ScriptSystem>(m_Scene, *m_StreamingManager, *m_ScriptEngine);
    m_EditorContext.Scripts = m_ScriptSystem.get();

    m_ViewportPanel.SetDropFileCallback([this](const String& path) { HandleDroppedFile(path); });
}

Application::~Application()
{
    JobSystem::Get().WaitAll();
    JobSystem::Shutdown();

    m_Renderer->GetDevice().WaitIdle();

    m_ScriptSystem.reset();
    m_ScriptEngine.reset();

    m_StreamingManager.reset();
    m_JoltDebugRenderer.reset();
    Physics::Shutdown();

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
#if defined(CARAMEL_MACOS)
    @autoreleasepool {
#else
    {
#endif
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
            if (event.type == SDL_EVENT_DROP_FILE) {
                HandleDroppedFile(event.drop.data);
            }
        }

        ProcessPendingFileDialogResult();

        m_Timer.Tick();

        m_StreamingManager->Update();

        // Ticked before Scene::Update so an entity spawned this frame registers its streaming
        // request in time, and before BuildRenderInstances so scripted motion has no frame of lag.
        m_ScriptSystem->Update(m_Timer);

        m_Scene.Update(*m_StreamingManager);

        ImGui_ImplSDL3_NewFrame();
        Input::NewFrame();
        ImGui::NewFrame();

        // Drawn before the dockspace so the viewport work area already excludes the bar.
        DrawMainMenuBar();

        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        SetupDefaultDockLayout(dockspaceId);

        if (Input::IsKeyPressed(SDL_SCANCODE_ESCAPE) && !ImGui::GetIO().WantTextInput) {
            m_EditorContext.SelectedEntity = nullptr;
            m_EditorContext.SelectedInstance = 0;
            m_EditorContext.SelectedMesh = nullptr;
        }

        m_Camera.Update(m_Timer.GetDelta());

        m_ViewportPanel.Draw(m_EditorContext, *m_Renderer);

        TArray<RenderInstance> renderInstances = m_Scene.BuildRenderInstances(*m_StreamingManager);

        UpdatePicking(renderInstances);
        if (m_ShowColliders)
            DrawColliders(renderInstances);

        m_OverlayPanel.Draw(m_EditorContext, *m_StreamingManager, m_Window, m_DeviceInfo);
        m_HierarchyPanel.Draw(m_EditorContext, *m_StreamingManager);
        m_InspectorPanel.Draw(m_EditorContext, *m_StreamingManager);
        m_ContentDrawerPanel.Draw();
        if (m_ShowRenderGraphPanel)
            m_RenderGraphPanel.Draw(m_EditorContext, *m_Renderer);
        ImGui::Render();

        renderInstances = m_Scene.BuildRenderInstances(*m_StreamingManager);

        m_Renderer->Render(m_Camera, *m_StreamingManager, renderInstances);
    }
    }
}

void Application::OpenSaveSceneDialog()
{
    static SDL_DialogFileFilter filters[] = { { "Caramel Scene", "cscene" } };
    SDL_ShowSaveFileDialog(&Application::OnSaveDialogResult, this, m_Window, filters, 1, "Content/Scenes/");
}

void Application::OpenLoadSceneDialog()
{
    static SDL_DialogFileFilter filters[] = { { "Caramel Scene", "cscene" } };
    SDL_ShowOpenFileDialog(&Application::OnOpenDialogResult, this, m_Window, filters, 1, "Content/Scenes/", false);
}

void Application::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu(ICON_FA_FILE " File")) {
        if (ImGui::MenuItem(ICON_FA_FILE_EXPORT " Save Scene..."))
            OpenSaveSceneDialog();
        if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Load Scene..."))
            OpenLoadSceneDialog();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_XMARK " Exit"))
            m_Running = false;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_SCROLL " Scripts")) {
        if (ImGui::MenuItem(ICON_FA_ROTATE " Reload All"))
            m_ScriptSystem->ReloadAll();
        ImGui::EndMenu();
    }

    // The streaming and collider controls used to sit inside the viewport overlay, mixed in with
    // read-only telemetry. They are commands, so they belong on a menu.
    if (ImGui::BeginMenu(ICON_FA_GEAR " Debug")) {
        ImGui::MenuItem(ICON_FA_CUBE " Show Colliders", nullptr, &m_ShowColliders);

        bool autoStream = m_StreamingManager->GetAutoStream();
        if (ImGui::MenuItem(ICON_FA_DOWNLOAD " Automatic Streaming", nullptr, &autoStream))
            m_StreamingManager->SetAutoStream(autoStream);

        if (ImGui::MenuItem(ICON_FA_FORWARD_STEP " Advance Streaming", nullptr, false, !autoStream))
            m_StreamingManager->PumpStreaming();

        ImGui::Separator();
        ImGui::MenuItem(ICON_FA_DIAGRAM_PROJECT " Resource Dependency Viewer", nullptr, &m_ShowRenderGraphPanel);

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void Application::SetupDefaultDockLayout(ImGuiID dockspaceId)
{
    if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
        return;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);

    ImGui::DockBuilderDockWindow(HierarchyPanel::kTitle, left);
    ImGui::DockBuilderDockWindow(InspectorPanel::kTitle, right);
    ImGui::DockBuilderDockWindow(ViewportPanel::kTitle, center);

    ImGui::DockBuilderFinish(dockspaceId);
}

void Application::UpdatePicking(const TArray<RenderInstance>& renderInstances)
{
    int width = (int)m_EditorContext.ViewportRectSize.x;
    int height = (int)m_EditorContext.ViewportRectSize.y;
    float aspectRatio = height != 0 ? (float)width / (float)height : 1.0f;

    bool hasSelection = m_EditorContext.SelectedEntity && m_EditorContext.SelectedInstance < m_EditorContext.SelectedEntity->instances.Size();

    if (hasSelection)
        DrawSelectionHighlight(renderInstances);

    if (!m_EditorContext.ViewportHovered || !Input::IsMouseButtonPressed(SDL_BUTTON_LEFT))
        return;

    // The mouse and the viewport origin are both in logical points, but width/height are physical
    // pixels -- scale into pixel space before the bounds test and the ray build.
    glm::vec2 mousePos = Input::GetMousePosition();
    glm::vec2 localMouse = mousePos - glm::vec2(m_EditorContext.ViewportRectMin.x, m_EditorContext.ViewportRectMin.y);
    localMouse *= m_EditorContext.ViewportDpiScale;
    if (localMouse.x < 0.0f || localMouse.y < 0.0f || localMouse.x >= (float)width || localMouse.y >= (float)height)
        return;

    glm::vec3 rayOrigin, rayDir;
    m_Camera.ScreenPointToRay(localMouse, (uint32)width, (uint32)height, aspectRatio, rayOrigin, rayDir);

    SceneNode* bestEntity = nullptr;
    uint32 bestInstance = 0;
    StreamingModel* bestMesh = nullptr;
    float bestFraction = FLT_MAX;

    for (const RenderInstance& ri : renderInstances) {
        if (!ri.mesh->HasCollider())
            continue;

        glm::mat4 invWorld = glm::inverse(ri.transform);
        glm::vec3 localOrigin = glm::vec3(invWorld * glm::vec4(rayOrigin, 1.0f));
        glm::vec3 localDir = glm::vec3(invWorld * glm::vec4(rayDir, 0.0f));

        JPH::RayCast ray;
        ray.mOrigin = JPH::Vec3(localOrigin.x, localOrigin.y, localOrigin.z);
        ray.mDirection = JPH::Vec3(localDir.x, localDir.y, localDir.z);

        JPH::RayCastResult hit;
        if (ri.mesh->GetColliderShape()->CastRay(ray, JPH::SubShapeIDCreator(), hit) && hit.mFraction < bestFraction) {
            bestFraction = hit.mFraction;
            bestEntity = ri.owner;
            bestInstance = ri.instanceIndex;
            bestMesh = ri.mesh;
        }
    }

    m_EditorContext.SelectedEntity = bestEntity;
    m_EditorContext.SelectedInstance = bestInstance;
    m_EditorContext.SelectedMesh = bestMesh;
}

void Application::DrawSelectionHighlight(const TArray<RenderInstance>& renderInstances)
{
    m_JoltDebugRenderer->SetDepthTest(false);

    // With a mesh picked the exact collider is affordable and useful. With only an instance picked
    // it is not: an entity like Sponza is thousands of meshes, and drawing every collider shape --
    // depth-test off, so nothing culls -- costs more per frame than the scene itself. One enclosing
    // box says the same thing about what is selected.
    if (m_EditorContext.SelectedMesh) {
        for (const RenderInstance& ri : renderInstances) {
            if (ri.owner != m_EditorContext.SelectedEntity || ri.instanceIndex != m_EditorContext.SelectedInstance)
                continue;
            if (ri.mesh != m_EditorContext.SelectedMesh || !ri.mesh->HasCollider())
                continue;

            ri.mesh->GetColliderShape()->Draw(m_JoltDebugRenderer.get(), Physics::ToJolt(ri.transform),
                                               JPH::Vec3::sReplicate(1.0f), JPH::Color(255, 217, 26), false, true);
        }
        return;
    }

    glm::vec3 boundsMin(FLT_MAX);
    glm::vec3 boundsMax(-FLT_MAX);
    bool hasBounds = false;

    for (const RenderInstance& ri : renderInstances) {
        if (ri.owner != m_EditorContext.SelectedEntity || ri.instanceIndex != m_EditorContext.SelectedInstance)
            continue;

        // Corners rather than the raw min/max: a rotated mesh's local box does not stay axis-aligned
        // once it is transformed into world space.
        const glm::vec3& localMin = ri.mesh->GetMesh().boundsMin;
        const glm::vec3& localMax = ri.mesh->GetMesh().boundsMax;
        for (int corner = 0; corner < 8; ++corner) {
            glm::vec3 local(
                (corner & 1) ? localMax.x : localMin.x,
                (corner & 2) ? localMax.y : localMin.y,
                (corner & 4) ? localMax.z : localMin.z);

            glm::vec3 world = glm::vec3(ri.transform * glm::vec4(local, 1.0f));
            boundsMin = glm::min(boundsMin, world);
            boundsMax = glm::max(boundsMax, world);
        }
        hasBounds = true;
    }

    if (!hasBounds)
        return;

    JPH::AABox box(JPH::Vec3(boundsMin.x, boundsMin.y, boundsMin.z), JPH::Vec3(boundsMax.x, boundsMax.y, boundsMax.z));
    m_JoltDebugRenderer->DrawWireBox(box, JPH::Color(255, 217, 26));
}

void Application::DrawColliders(const TArray<RenderInstance>& renderInstances)
{
    m_JoltDebugRenderer->SetDepthTest(true);

    for (const RenderInstance& ri : renderInstances) {
        if (!ri.mesh->HasCollider())
            continue;

        ri.mesh->GetColliderShape()->Draw(m_JoltDebugRenderer.get(), Physics::ToJolt(ri.transform),
                                           JPH::Vec3::sReplicate(1.0f), JPH::Color(80, 200, 120), false, true);
    }
}

void Application::HandleDroppedFile(const String& path)
{
    namespace fs = std::filesystem;

    fs::path droppedPath(path.CStr());
    String cmdlPath;

    if (droppedPath.extension() == ".cmdl") {
        cmdlPath = path;
    } else {
        fs::path cacheDir("Content/Cache");
        fs::path manifestPath = cacheDir / "cache.json";

        std::ifstream file(manifestPath);
        if (!file) {
            CARAMEL_ERROR("Dropped file '{}' is not a .cmdl and Content/Cache/cache.json doesn't exist", path.CStr());
            return;
        }

        nlohmann::json manifest;
        file >> manifest;

        std::error_code ec;
        std::string key = fs::relative(droppedPath, cacheDir, ec).generic_string();
        if (ec) {
            CARAMEL_ERROR("Dropped file '{}' isn't reachable from Content/Cache/", path.CStr());
            return;
        }

        const auto& entries = manifest["entries"];
        auto it = entries.find(key);
        if (it == entries.end()) {
            CARAMEL_ERROR("Dropped file '{}' (key '{}') has no entry in Content/Cache/cache.json -- compile it first", path.CStr(), key.c_str());
            return;
        }

        cmdlPath = String((cacheDir / it->value("cmdl", "")).string());
    }

    String name = String(fs::path(cmdlPath.CStr()).stem().string());
    SceneNode* entity = m_Scene.CreateModelEntity(nullptr, name, cmdlPath, *m_StreamingManager);
    m_Scene.AddInstance(entity, Instance{});
}

void Application::StorePendingDialogResult(const char* const* filelist, bool isSave)
{
    if (!filelist || !filelist[0])
        return;

    std::lock_guard lock(m_FileDialogMutex);
    m_PendingDialogPath = filelist[0];
    m_PendingDialogIsSave = isSave;
    m_HasPendingDialogResult = true;
}

void SDLCALL Application::OnSaveDialogResult(void* userdata, const char* const* filelist, int)
{
    reinterpret_cast<Application*>(userdata)->StorePendingDialogResult(filelist, true);
}

void SDLCALL Application::OnOpenDialogResult(void* userdata, const char* const* filelist, int)
{
    reinterpret_cast<Application*>(userdata)->StorePendingDialogResult(filelist, false);
}

void Application::ProcessPendingFileDialogResult()
{
    String path;
    bool isSave;
    {
        std::lock_guard lock(m_FileDialogMutex);
        if (!m_HasPendingDialogResult)
            return;
        path = m_PendingDialogPath;
        isSave = m_PendingDialogIsSave;
        m_HasPendingDialogResult = false;
    }

    if (isSave) {
        std::error_code ec;
        std::filesystem::create_directories("Content/Scenes", ec);
        if (!m_Scene.SaveToFile(path))
            CARAMEL_ERROR("Failed to save scene to '{}'", path.CStr());
    } else {
        if (!m_Scene.LoadFromFile(path, *m_StreamingManager)) {
            CARAMEL_ERROR("Failed to load scene from '{}'", path.CStr());
        } else {
            m_EditorContext.SelectedEntity = nullptr;
            m_EditorContext.SelectedMesh = nullptr;
            m_ScriptSystem->RebuildFromScene();
        }
    }
}
