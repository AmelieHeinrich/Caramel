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

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <FontAwesome/FA.h>

#include <nlohmann/json.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{
    constexpr const char* kViewportTitle = ICON_FA_CUBE " Viewport";
    constexpr const char* kHierarchyTitle = ICON_FA_SITEMAP " Hierarchy";
    constexpr const char* kInspectorTitle = ICON_FA_CIRCLE " Inspector";
    constexpr const char* kContentFileDragDropID = "CONTENT_FILE_PATH";
}

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
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplSDL3_InitForOther(m_Window);

    m_Renderer = MakeUnique<Renderer>(m_Window);
    m_DeviceInfo = m_Renderer->GetDeviceInfo();

    JobSystem::Initialize();
    Physics::Initialize();
    m_JoltDebugRenderer = MakeUnique<JoltDebugRenderer>();

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

    m_StreamingManager.reset(); // releases every StreamingModel's JPH::RefConst<Shape>
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

        m_StreamingManager->Update();
        m_Scene.Update(*m_StreamingManager);

        ImGui_ImplSDL3_NewFrame();
        Input::NewFrame();
        ImGui::NewFrame();

        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        SetupDefaultDockLayout(dockspaceId);

        if (Input::IsKeyPressed(SDL_SCANCODE_ESCAPE) && !ImGui::GetIO().WantTextInput) {
            m_SelectedEntity = nullptr;
            m_SelectedInstance = 0;
            m_SelectedMesh = nullptr;
        }

        m_Camera.Update(ImGui::GetIO().DeltaTime);

        // Populates m_ViewportRectMin/Size for this frame before anything below needs viewport-space
        // math (pick ray, drop target).
        ShowViewport();

        TArray<RenderInstance> renderInstances = m_Scene.BuildRenderInstances(*m_StreamingManager);

        UpdatePicking(renderInstances);
        if (m_ShowColliders)
            DrawColliders(renderInstances);

        ShowOverlay();
        ShowHierarchy();
        ShowInspector();
        ShowContentDrawer();
        ImGui::Render();

        m_Renderer->Render(m_Camera, *m_StreamingManager, renderInstances);
    }
}

void Application::SetupDefaultDockLayout(ImGuiID dockspaceId)
{
    // Only seed a layout the first time this dockspace id is ever seen -- once it exists (either
    // from this call or restored from imgui.ini), leave whatever arrangement the user has since
    // dragged panels into alone.
    if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
        return;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);

    ImGui::DockBuilderDockWindow(kHierarchyTitle, left);
    ImGui::DockBuilderDockWindow(kInspectorTitle, right);
    ImGui::DockBuilderDockWindow(kViewportTitle, center);

    ImGui::DockBuilderFinish(dockspaceId);
}

void Application::ShowViewport()
{
    ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(kViewportTitle, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    // The Viewport is now a real ImGui window (it used to just be the bare backbuffer with nothing
    // else drawn under most of the screen), so io.WantCaptureMouse is true whenever the mouse is
    // over it -- UpdatePicking() needs this instead to know "the click landed on the 3D view
    // specifically, not some other panel/toolbar drawn on top of it", which WantCaptureMouse can't
    // distinguish anymore.
    m_ViewportHovered = ImGui::IsWindowHovered();

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    m_ViewportRectMin = ImGui::GetCursorScreenPos();
    m_ViewportRectSize = ImVec2(contentSize.x > 0.0f ? contentSize.x : 1.0f, contentSize.y > 0.0f ? contentSize.y : 1.0f);

    int windowW = 0, windowH = 0, pixelW = 0, pixelH = 0;
    SDL_GetWindowSize(m_Window, &windowW, &windowH);
    SDL_GetWindowSizeInPixels(m_Window, &pixelW, &pixelH);
    float dpiScale = (windowW > 0 && pixelW > 0) ? ((float)pixelW / (float)windowW) : 1.0f;
    const uint32 viewportTextureWidth = (uint32)std::max(1.0f, std::ceil(m_ViewportRectSize.x * dpiScale));
    const uint32 viewportTextureHeight = (uint32)std::max(1.0f, std::ceil(m_ViewportRectSize.y * dpiScale));

    m_Renderer->SetViewportSize(viewportTextureWidth, viewportTextureHeight);
    ImGui::Image(m_Renderer->GetViewportTextureID(), m_ViewportRectSize);

    // Attached directly to the Image() item above rather than a separate invisible overlay window:
    // now that Viewport is a real docked panel (not a full-screen backdrop), a same-rect window
    // created afterward would sit behind it in z-order and never receive hover, so its drag-drop
    // target would never trigger.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(kContentFileDragDropID))
            HandleDroppedFile(String((const char*)accepted->Data));
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}

void Application::ShowOverlay()
{
    // Anchored to the Viewport panel's content rect (not the OS window/main viewport) so it draws
    // over the 3D scene specifically -- now that Hierarchy/Inspector are docked panels occupying
    // real screen space, an absolute top-left window position could land on top of them instead.
    ImGui::SetNextWindowPos(ImVec2(m_ViewportRectMin.x + 12.0f, m_ViewportRectMin.y + 12.0f));
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
    {
        int windowW = 0, windowH = 0, pixelW = 0, pixelH = 0;
        SDL_GetWindowSize(m_Window, &windowW, &windowH);
        SDL_GetWindowSizeInPixels(m_Window, &pixelW, &pixelH);
        float dpiScale = (windowW > 0 && pixelW > 0) ? ((float)pixelW / (float)windowW) : 1.0f;

        int viewportPixelW = (int)std::max(1.0f, std::ceil(m_ViewportRectSize.x * dpiScale));
        int viewportPixelH = (int)std::max(1.0f, std::ceil(m_ViewportRectSize.y * dpiScale));

        if (dpiScale != 1.0f)
            ImGui::Text("Viewport: %dx%d px (%dx%d @ %.2gx)", viewportPixelW, viewportPixelH, (int)m_ViewportRectSize.x, (int)m_ViewportRectSize.y, dpiScale);
        else
            ImGui::Text("Viewport: %dx%d px", viewportPixelW, viewportPixelH);
        ImGui::Text("Window: %dx%d px", pixelW, pixelH);
    }
    ImGui::Separator();
    ImGui::Text("Device: %s", m_DeviceInfo.name);
    ImGui::Text("Driver: %s", m_DeviceInfo.driverVersion);
    ImGui::Text("Raytracing: %s - Mesh Shaders: %s", m_DeviceInfo.supportsRayTracing ? "Yes" : "No", m_DeviceInfo.supportsMeshShaders ? "Yes" : "No");

    ImGui::Separator();
    ImGui::Text("%d meshes, %d textures streamed", (int)m_StreamingManager->GetModels().Size(), (int)m_StreamingManager->GetTextures().Size());

    bool autoStream = m_StreamingManager->GetAutoStream();
    if (ImGui::Checkbox("Automatic streaming", &autoStream))
        m_StreamingManager->SetAutoStream(autoStream);

    if (!autoStream && ImGui::Button("Advance"))
        m_StreamingManager->PumpStreaming();

    ImGui::Checkbox(ICON_FA_CUBE " Show colliders", &m_ShowColliders);

    ImGui::Text("Upload budget: %.1f / %.1f MB in flight",
                m_StreamingManager->GetBytesInFlight() / (1024.0 * 1024.0),
                StreamingManager::GetMaxBytesInFlight() / (1024.0 * 1024.0));

    ImGui::End();
}

void Application::UpdatePicking(const TArray<RenderInstance>& renderInstances)
{
    int width = (int)m_ViewportRectSize.x;
    int height = (int)m_ViewportRectSize.y;
    float aspectRatio = height != 0 ? (float)width / (float)height : 1.0f;

    bool hasSelection = m_SelectedEntity && m_SelectedInstance < m_SelectedEntity->instances.Size();

    if (hasSelection) {
        // Highlight either just the drilled-down mesh (m_SelectedMesh set -- e.g. from a viewport
        // click, which always resolves to one specific mesh) or every mesh of the selected instance
        // (m_SelectedMesh null -- the whole instance was selected in the Hierarchy). Draws the
        // actual collider geometry in wireframe, always on top, rather than just its AABB;
        // MeshShape::Draw() caches its triangle batch internally, so this is cheap every frame.
        m_JoltDebugRenderer->SetDepthTest(false);
        for (const RenderInstance& ri : renderInstances) {
            if (ri.owner != m_SelectedEntity || ri.instanceIndex != m_SelectedInstance || !ri.mesh->HasCollider())
                continue;
            if (m_SelectedMesh && ri.mesh != m_SelectedMesh)
                continue;

            ri.mesh->GetColliderShape()->Draw(m_JoltDebugRenderer.get(), Physics::ToJolt(ri.transform),
                                               JPH::Vec3::sReplicate(1.0f), JPH::Color(255, 217, 26), false, true);
        }
    }

    if (!m_ViewportHovered || !Input::IsMouseButtonPressed(SDL_BUTTON_LEFT))
        return;

    glm::vec2 mousePos = Input::GetMousePosition();
    glm::vec2 localMouse = mousePos - glm::vec2(m_ViewportRectMin.x, m_ViewportRectMin.y);
    if (localMouse.x < 0.0f || localMouse.y < 0.0f || localMouse.x >= (float)width || localMouse.y >= (float)height)
        return; // Click landed outside the Viewport panel's content rect (over a docked panel).

    glm::vec3 rayOrigin, rayDir;
    m_Camera.ScreenPointToRay(localMouse, (uint32)width, (uint32)height, aspectRatio, rayOrigin, rayDir);

    SceneNode* bestEntity = nullptr;
    uint32 bestInstance = 0;
    StreamingModel* bestMesh = nullptr;
    float bestFraction = FLT_MAX;

    for (const RenderInstance& ri : renderInstances) {
        if (!ri.mesh->HasCollider())
            continue;

        // Transforming the ray's origin as a point and its direction as a vector (w = 0) preserves
        // the ray's fraction parametrization exactly regardless of scale, so hit fractions stay
        // directly comparable across instances below.
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

    m_SelectedEntity = bestEntity;
    m_SelectedInstance = bestInstance;
    m_SelectedMesh = bestMesh;
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

namespace
{
    // Payload is a raw SceneNode* (the tree owns the node; the payload just identifies which one).
    constexpr const char* kSceneNodeDragDropID = "SCENE_NODE_PTR";
}

void Application::ShowHierarchy()
{
    ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin(kHierarchyTitle);

    if (ImGui::Button(ICON_FA_FOLDER " New Folder"))
        m_Scene.CreateFolder(nullptr, "New Folder");

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_EXPORT " Save Scene...")) {
        static SDL_DialogFileFilter filters[] = { { "Caramel Scene", "cscene" } };
        SDL_ShowSaveFileDialog(&Application::OnSaveDialogResult, this, m_Window, filters, 1, "Content/Scenes/");
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_IMPORT " Load Scene...")) {
        static SDL_DialogFileFilter filters[] = { { "Caramel Scene", "cscene" } };
        SDL_ShowOpenFileDialog(&Application::OnOpenDialogResult, this, m_Window, filters, 1, "Content/Scenes/", false);
    }

    ImGui::Separator();
    DrawSceneNode(m_Scene.GetRoot());

    ImGui::Separator();
    ImGui::TextDisabled(ICON_FA_DOWNLOAD " Drop here to move to root");
    ImGui::Dummy(ImVec2(-1.0f, 24.0f));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(kSceneNodeDragDropID)) {
            SceneNode* dragged = *(SceneNode**)accepted->Data;
            m_Scene.Reparent(dragged, nullptr);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}

void Application::DrawSceneNode(SceneNode& node)
{
    // Deleting mutates node.children (the container this loop walks), so any "Delete" clicked
    // below is only recorded here and applied once after the loop finishes, never mid-iteration.
    SceneNode* pendingDelete = nullptr;

    for (size_t i = 0; i < node.children.Size(); ++i) {
        SceneNode& child = *node.children[i];
        ImGui::PushID(&child);

        bool isFolder = (child.type == ESceneNodeType::Folder);
        bool renaming = (m_RenamingNode == &child);

        char nodeLabel[300];
        std::snprintf(nodeLabel, sizeof(nodeLabel), "%s %s", isFolder ? ICON_FA_FOLDER : ICON_FA_CUBES, child.name.CStr());

        bool open = ImGui::TreeNodeEx(renaming ? "##renaming" : nodeLabel,
                                       ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

        if (!renaming && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_RenamingNode = &child;
            m_RenameJustStarted = true;
            std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", child.name.CStr());
        }

        if (!renaming && ImGui::BeginDragDropSource()) {
            SceneNode* payload = &child;
            ImGui::SetDragDropPayload(kSceneNodeDragDropID, &payload, sizeof(SceneNode*));
            ImGui::TextUnformatted(child.name.CStr());
            ImGui::EndDragDropSource();
        }

        // Only folders can contain other nodes -- entities own instances, not children.
        if (isFolder && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(kSceneNodeDragDropID)) {
                SceneNode* dragged = *(SceneNode**)accepted->Data;
                m_Scene.Reparent(dragged, &child);
            }
            ImGui::EndDragDropTarget();
        }

        if (!renaming && ImGui::BeginPopupContextItem()) {
            if (isFolder && ImGui::MenuItem(ICON_FA_FOLDER " New Folder"))
                m_Scene.CreateFolder(&child, "New Folder");
            if (!isFolder && ImGui::MenuItem(ICON_FA_PLUS " Add Instance"))
                m_Scene.AddInstance(&child, Instance{});
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete"))
                pendingDelete = &child;
            ImGui::EndPopup();
        }

        if (renaming) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (m_RenameJustStarted) {
                ImGui::SetKeyboardFocusHere();
                m_RenameJustStarted = false;
            }
            bool committed = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (committed) {
                child.name = m_RenameBuffer;
                m_RenamingNode = nullptr;
            } else if (ImGui::IsItemDeactivated()) {
                m_RenamingNode = nullptr;
            }
        }

        if (open) {
            if (isFolder) {
                DrawSceneNode(child);
            } else {
                // Entity -> Instance -> Mesh: each instance nests the entity's actual meshes (the
                // same per-primitive names the streaming view uses), since one instance can be many
                // meshes and picking/inspecting usually wants to drill down to just one of them.
                const TArray<TShared<StreamingModel>>& models = m_StreamingManager->GetModels();
                for (uint32 j = 0; j < (uint32)child.instances.Size(); ++j) {
                    ImGui::PushID((int)j);

                    char instLabel[300];
                    std::snprintf(instLabel, sizeof(instLabel), "%s %s", ICON_FA_LOCATION_ARROW, child.instances[j].name.CStr());

                    ImGuiTreeNodeFlags instFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (m_SelectedEntity == &child && m_SelectedInstance == j && !m_SelectedMesh)
                        instFlags |= ImGuiTreeNodeFlags_Selected;

                    bool instanceOpen = ImGui::TreeNodeEx(instLabel, instFlags);
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        m_SelectedEntity = &child;
                        m_SelectedInstance = j;
                        m_SelectedMesh = nullptr;
                    }

                    if (instanceOpen) {
                        for (uint32 meshIndex : child.meshIndices) {
                            StreamingModel* mesh = models[meshIndex].get();

                            char meshLabel[300];
                            std::snprintf(meshLabel, sizeof(meshLabel), "%s %s", ICON_FA_CUBE, mesh->GetMesh().name.CStr());

                            // TreeNodeEx-as-leaf (not a plain Selectable) so its text lines up with
                            // the arrow-indented rows above it instead of sitting flush-left.
                            ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                                          | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanAvailWidth;
                            if (m_SelectedEntity == &child && m_SelectedInstance == j && m_SelectedMesh == mesh)
                                meshFlags |= ImGuiTreeNodeFlags_Selected;

                            ImGui::PushID((int)meshIndex);
                            ImGui::TreeNodeEx(meshLabel, meshFlags);
                            if (ImGui::IsItemClicked()) {
                                m_SelectedEntity = &child;
                                m_SelectedInstance = j;
                                m_SelectedMesh = mesh;
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (pendingDelete) {
        if (m_SelectedEntity == pendingDelete) {
            m_SelectedEntity = nullptr;
            m_SelectedMesh = nullptr;
        }
        if (m_RenamingNode == pendingDelete)
            m_RenamingNode = nullptr;
        m_Scene.DeleteNode(pendingDelete);
    }
}

void Application::ShowContentDrawer()
{
    // Dear ImGui's docking branch has no built-in Unreal-style auto-hide/collapse-to-tab-strip, so
    // this is a custom animated floating window (NoDocking) rather than a dock node: a slim bar is
    // always pinned to the bottom edge, and clicking it tweens a drawer above it open/closed using a
    // fixed-duration ease-out-back curve, which overshoots past full height before settling -- the
    // "little bounce" -- instead of a plain linear/exponential slide.
    constexpr float kBarHeight = 30.0f;
    constexpr float kDrawerHeight = 320.0f;
    constexpr float kAnimDuration = 0.35f;
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    m_ContentDrawerAnimTime += ImGui::GetIO().DeltaTime;
    float t = std::min(m_ContentDrawerAnimTime / kAnimDuration, 1.0f);
    float tm1 = t - 1.0f;
    float eased = 1.0f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1; // overshoots > 1 near t=1, settles at 1.
    float progress = std::max(m_ContentDrawerOpen ? eased : 1.0f - eased, 0.0f);
    float drawerHeight = kDrawerHeight * progress;

    ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                               | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - kBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, kBarHeight));
    ImGui::Begin("##ContentDrawerBar", nullptr, barFlags);
    if (ImGui::Button(m_ContentDrawerOpen ? ICON_FA_FOLDER_OPEN " Content" : ICON_FA_FOLDER " Content")) {
        m_ContentDrawerOpen = !m_ContentDrawerOpen;
        m_ContentDrawerAnimTime = 0.0f;
    }
    ImGui::End();

    if (drawerHeight <= 0.0f)
        return; // Fully closed -- skip drawing a lingering zero-height window.

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - kBarHeight - drawerHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, drawerHeight));
    ImGui::Begin("##ContentDrawer", nullptr, barFlags & ~ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("Drag a mesh file onto the viewport to add it to the scene");
    ImGui::Separator();
    DrawContentDirectory("Content");
    ImGui::End();
}

void Application::DrawContentDirectory(const String& directory)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(directory.CStr(), ec))
        return;

    TArray<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(directory.CStr(), ec))
        entries.PushBack(entry);

    std::sort(entries.Begin(), entries.End(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        if (a.is_directory() != b.is_directory())
            return a.is_directory() > b.is_directory(); // Folders first.
        return a.path().filename().string() < b.path().filename().string();
    });

    for (const fs::directory_entry& entry : entries) {
        const fs::path& p = entry.path();
        String label = String(p.filename().string());

        ImGui::PushID(label.CStr());

        char entryLabel[300];
        std::snprintf(entryLabel, sizeof(entryLabel), "%s %s", entry.is_directory() ? ICON_FA_FOLDER : ICON_FA_CUBE, label.CStr());

        if (entry.is_directory()) {
            if (ImGui::TreeNodeEx(entryLabel, ImGuiTreeNodeFlags_SpanAvailWidth)) {
                DrawContentDirectory(String(p.string()));
                ImGui::TreePop();
            }
        } else {
            std::string ext = p.extension().string();
            if (ext == ".gltf" || ext == ".glb" || ext == ".cmdl") {
                // TreeNodeEx-as-leaf so file rows line up with folder rows' arrow indentation
                // instead of sitting flush-left like a plain Selectable would.
                ImGui::TreeNodeEx(entryLabel, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                             | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanAvailWidth);
                if (ImGui::BeginDragDropSource()) {
                    std::string pathStr = p.string();
                    ImGui::SetDragDropPayload(kContentFileDragDropID, pathStr.c_str(), pathStr.size() + 1);
                    ImGui::TextUnformatted(pathStr.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }

        ImGui::PopID();
    }
}

void Application::ShowInspector()
{
    ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin(kInspectorTitle);

    if (!m_SelectedEntity || m_SelectedInstance >= m_SelectedEntity->instances.Size()) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::End();
        return;
    }

    Instance& instance = m_SelectedEntity->instances[m_SelectedInstance];

    ImGui::Text(ICON_FA_CUBES " Entity: %s", m_SelectedEntity->name.CStr());
    ImGui::Text(ICON_FA_FILE " Asset: %s", m_SelectedEntity->cmdlPath.CStr());
    if (m_SelectedMesh)
        ImGui::Text(ICON_FA_CUBE " Mesh: %s", m_SelectedMesh->GetMesh().name.CStr());

    char nameBuffer[256];
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", instance.name.CStr());
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
        instance.name = nameBuffer;

    ImGui::Separator();
    ImGui::TextUnformatted(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform");

    // Unity/Unreal-style per-axis row: a colored X/Y/Z tag in front of each component so the three
    // DragFloats read at a glance instead of a bare unlabeled DragFloat3, plus a reset-to-default
    // button since there's no gizmo anymore to nudge a value back by eye.
    static const ImVec4 kAxisColors[3] = {
        ImVec4(0.80f, 0.25f, 0.30f, 1.0f), // X
        ImVec4(0.30f, 0.70f, 0.30f, 1.0f), // Y
        ImVec4(0.25f, 0.50f, 0.90f, 1.0f), // Z
    };
    static const char* kAxisNames[3] = { "X", "Y", "Z" };

    auto transformRow = [&](const char* label, glm::vec3& v, float speed, float resetTo) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(90.0f);

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        float* components[3] = { &v.x, &v.y, &v.z };
        for (int i = 0; i < 3; ++i) {
            if (i > 0)
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Text, kAxisColors[i]);
            ImGui::DragFloat(kAxisNames[i], components[i], speed, 0.0f, 0.0f, "%.2f");
            ImGui::PopStyleColor();
            ImGui::PopItemWidth();
            ImGui::PopID();
        }

        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button(ICON_FA_ROTATE_LEFT))
            v = glm::vec3(resetTo);
        ImGui::PopID();
    };

    transformRow("Position", instance.position, 0.05f, 0.0f);
    transformRow("Rotation", instance.rotationEuler, 0.5f, 0.0f);
    transformRow("Scale", instance.scale, 0.01f, 1.0f);

    ImGui::Separator();
    ImGui::TextUnformatted(ICON_FA_PALETTE " Material");
    // Prefer the drilled-down mesh's material, if one is selected, else fall back to the entity's
    // first mesh -- meshIndices is only empty while streaming for this entity hasn't started yet.
    StreamingModel* materialSource = m_SelectedMesh;
    if (!materialSource && !m_SelectedEntity->meshIndices.IsEmpty())
        materialSource = m_StreamingManager->GetModels()[m_SelectedEntity->meshIndices[0]].get();

    if (materialSource) {
        const ModelMaterial& material = materialSource->GetMaterial();
        uint32 requestId = materialSource->GetRequestId();
        int32 materialIndex = materialSource->GetMesh().materialIndex;

        ImGui::Text("Name: %s", material.name.CStr());

        // Edits are scene-local overrides (Scene::GetOrCreateMaterialOverride), not writes to the
        // .cmdl asset itself -- ApplyMaterialOverrides() mutates this entity's own private CPUModel
        // copy (never shared with other entities/scenes referencing the same file, see
        // CPUModel::GetMaterials()), and the override list round-trips through Scene::SaveToFile.
        glm::vec4 baseColor = material.baseColorFactor;
        if (ImGui::ColorEdit4("Base Color", &baseColor.x)) {
            MaterialOverride& matOverride = m_Scene.GetOrCreateMaterialOverride(*m_SelectedEntity, materialIndex);
            matOverride.overrideBaseColor = true;
            matOverride.baseColorFactor = baseColor;
            m_Scene.ApplyMaterialOverrides(*m_SelectedEntity, *m_StreamingManager);
        }

        float metallic = material.metallicFactor;
        if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f)) {
            MaterialOverride& matOverride = m_Scene.GetOrCreateMaterialOverride(*m_SelectedEntity, materialIndex);
            matOverride.overrideMetallic = true;
            matOverride.metallicFactor = metallic;
            m_Scene.ApplyMaterialOverrides(*m_SelectedEntity, *m_StreamingManager);
        }

        float roughness = material.roughnessFactor;
        if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f)) {
            MaterialOverride& matOverride = m_Scene.GetOrCreateMaterialOverride(*m_SelectedEntity, materialIndex);
            matOverride.overrideRoughness = true;
            matOverride.roughnessFactor = roughness;
            m_Scene.ApplyMaterialOverrides(*m_SelectedEntity, *m_StreamingManager);
        }

        glm::vec3 emissive = material.emissiveFactor;
        if (ImGui::ColorEdit3("Emissive", &emissive.x)) {
            MaterialOverride& matOverride = m_Scene.GetOrCreateMaterialOverride(*m_SelectedEntity, materialIndex);
            matOverride.overrideEmissive = true;
            matOverride.emissiveFactor = emissive;
            m_Scene.ApplyMaterialOverrides(*m_SelectedEntity, *m_StreamingManager);
        }

        ImGui::Text("Alpha Mode: %s  Double-Sided: %s", material.alphaMode.CStr(), material.doubleSided ? "Yes" : "No");

        // label + path text, plus a live ImGui::Image thumbnail once the texture has streamed in at
        // least one mip -- StreamingTexture::GetDisplayTexID() is already a ready-to-use bindless
        // ImTextureID (see ImGuiRenderer's own font texture for the same convention).
        auto drawTextureSlot = [&](const char* label, const String& path, MaterialTextureSlot slot) {
            ImGui::Text("%s: %s", label, path.Empty() ? "-" : path.CStr());
            if (path.Empty())
                return;

            TShared<StreamingTexture> texture = m_StreamingManager->GetMaterialTexture(requestId, materialIndex, slot);
            if (!texture || texture->GetDisplayTexID() == ImTextureID_Invalid) {
                ImGui::TextDisabled("Streaming...");
                return;
            }

            float aspect = texture->GetWidth() > 0 ? (float)texture->GetHeight() / (float)texture->GetWidth() : 1.0f;
            ImGui::Image(texture->GetDisplayTexID(), ImVec2(96.0f, 96.0f * aspect));
        };

        drawTextureSlot("Base Color Tex", material.baseColorTexture, MaterialTextureSlot::BaseColor);
        drawTextureSlot("Normal Tex", material.normalTexture, MaterialTextureSlot::Normal);
        drawTextureSlot("Metallic/Roughness Tex", material.metallicRoughnessTexture, MaterialTextureSlot::MetallicRoughness);
        drawTextureSlot("Occlusion Tex", material.occlusionTexture, MaterialTextureSlot::Occlusion);
        drawTextureSlot("Emissive Tex", material.emissiveTexture, MaterialTextureSlot::Emissive);
    }

    ImGui::Separator();
    if (ImGui::TreeNodeEx(ICON_FA_LAYER_GROUP " Meshes", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (ImGui::BeginTable("InspectorMeshTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Mesh");
            ImGui::TableSetupColumn("Resident LOD");
            ImGui::TableSetupColumn("Meshlets");
            ImGui::TableHeadersRow();

            const TArray<TShared<StreamingModel>>& models = m_StreamingManager->GetModels();
            for (uint32 meshIndex : m_SelectedEntity->meshIndices) {
                StreamingModel& mesh = *models[meshIndex];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (&mesh == m_SelectedMesh)
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1.0f), ICON_FA_CUBE " %s", mesh.GetMesh().name.CStr());
                else
                    ImGui::TextUnformatted(mesh.GetMesh().name.CStr());

                ImGui::TableSetColumnIndex(1);
                uint32 residentLOD = mesh.SnapshotResidentLOD();
                if (residentLOD != StreamingModel::kNoResidentLOD)
                    ImGui::Text("%u/%u", residentLOD, (uint32)CaramelAsset::kLodCount - 1);
                else
                    ImGui::TextUnformatted("Loading...");

                ImGui::TableSetColumnIndex(2);
                if (residentLOD != StreamingModel::kNoResidentLOD)
                    ImGui::Text("%u", mesh.GetMeshletCount(residentLOD));
                else
                    ImGui::TextUnformatted("-");
            }

            ImGui::EndTable();
        }

        ImGui::TreePop();
    }

    ImGui::End();
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
        return; // Cancelled, or an error (already logged by SDL/GetError isn't checked here -- best-effort).

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
            m_SelectedEntity = nullptr;
            m_SelectedMesh = nullptr;
        }
    }
}
