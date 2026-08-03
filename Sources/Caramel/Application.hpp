/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:48:03
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Renderer.hpp>
#include <Caramel/Renderer/Camera.hpp>
#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Physics/JoltDebugRenderer.hpp>
#include <Caramel/Scene/Scene.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <mutex>

struct ApplicationInfo
{
    int32 Width;
    int32 Height;
    bool Maximized;
    bool VSync;
    bool DebugLayer;
};

class Application
{
public:
    Application(const ApplicationInfo& info);
    ~Application();

    void Run();
private:
    void ShowOverlay();
    void ShowViewport();
    void SetupDefaultDockLayout(ImGuiID dockspaceId);
    void ShowHierarchy();
    void ShowInspector();
    void ShowContentDrawer();
    void ShowRendererSettings();
    void DrawSceneNode(SceneNode& node);
    void DrawContentDirectory(const String& directory);
    void UpdatePicking(const TArray<RenderInstance>& renderInstances);
    void DrawColliders(const TArray<RenderInstance>& renderInstances);

    void HandleDroppedFile(const String& path);

    // Runs the actual Save/Load off the SDL file dialog's async result -- called once per frame
    // from Run(), never from the dialog callback itself (which may fire on a different thread).
    void ProcessPendingFileDialogResult();
    void StorePendingDialogResult(const char* const* filelist, bool isSave);
    static void SDLCALL OnSaveDialogResult(void* userdata, const char* const* filelist, int filter);
    static void SDLCALL OnOpenDialogResult(void* userdata, const char* const* filelist, int filter);

private:
    ApplicationInfo m_Info;
    bool m_Running = true;

    SDL_Window* m_Window;
    TUnique<Renderer> m_Renderer;
    TUnique<StreamingManager> m_StreamingManager;
    Camera m_Camera;
    TUnique<JoltDebugRenderer> m_JoltDebugRenderer;

    Scene m_Scene;
    SceneNode* m_SelectedEntity = nullptr;
    uint32 m_SelectedInstance = 0;
    // Non-null when the selection has been drilled down to one specific mesh within the selected
    // instance (e.g. clicked in the viewport, or a "Mesh_N" row picked in the Hierarchy); null
    // means the whole instance is selected (every mesh highlights).
    StreamingModel* m_SelectedMesh = nullptr;

    // Hierarchy in-place rename state -- set on double-click, cleared on commit/cancel.
    SceneNode* m_RenamingNode = nullptr;
    bool m_RenameJustStarted = false;
    char m_RenameBuffer[256] = {};

    // Screen-space rect of the Viewport panel's content region, refreshed every frame by
    // ShowViewport() before UpdatePicking() runs -- the single source of truth every other
    // viewport-space computation (gizmo rect, pick ray, drop target, resolution overlay) reads from.
    ImVec2 m_ViewportRectMin{ 0.0f, 0.0f };
    ImVec2 m_ViewportRectSize{ 1.0f, 1.0f };
    bool m_ViewportHovered = false;

    // Bottom content-drawer open/close animation state (see ShowContentDrawer()). Time elapsed
    // since the last open/close toggle; starts already past the animation duration so the drawer
    // is at rest (fully closed) on the very first frame instead of animating open from a cold t=0.
    bool m_ContentDrawerOpen = false;
    float m_ContentDrawerAnimTime = 1.0f;

    agfxDeviceInfo m_DeviceInfo;

    bool m_ShowColliders = false;

    std::mutex m_FileDialogMutex;
    String m_PendingDialogPath;
    bool m_PendingDialogIsSave = false;
    bool m_HasPendingDialogResult = false;
};
