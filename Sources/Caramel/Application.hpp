/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:48:03
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Core/Timer.hpp>
#include <Caramel/Renderer/Renderer.hpp>
#include <Caramel/Renderer/Camera.hpp>
#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Physics/JoltDebugRenderer.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Script/ScriptEngine.hpp>
#include <Caramel/Script/ScriptSystem.hpp>
#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Editor/ViewportPanel.hpp>
#include <Caramel/Editor/OverlayPanel.hpp>
#include <Caramel/Editor/HierarchyPanel.hpp>
#include <Caramel/Editor/InspectorPanel.hpp>
#include <Caramel/Editor/ContentDrawerPanel.hpp>
#include <Caramel/Editor/RenderGraphPanel.hpp>
#include <Caramel/Editor/GpuTimingPanel.hpp>
#include <Caramel/Editor/CpuProfilerPanel.hpp>
#include <Caramel/Editor/ConsolePanel.hpp>
#include <Caramel/Editor/SettingsPanel.hpp>

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

    static void SDLCALL OnSaveDialogResult(void* userdata, const char* const* filelist, int filter);
    static void SDLCALL OnOpenDialogResult(void* userdata, const char* const* filelist, int filter);

public:
    void OpenSaveSceneDialog();
    void OpenLoadSceneDialog();

private:
    void DrawMainMenuBar();
    void SetupDefaultDockLayout(ImGuiID dockspaceId);
    void UpdatePicking(const TArray<RenderInstance>& renderInstances);
    void DrawSelectionHighlight(const TArray<RenderInstance>& renderInstances);
    void DrawLightGizmo(const SceneNode& node);
    void DrawColliders(const TArray<RenderInstance>& renderInstances);

    void HandleDroppedFile(const String& path);

    void LoadSceneFromFile(const String& path);

    void ProcessPendingSceneLoad();
    void ProcessPendingFileDialogResult();
    void StorePendingDialogResult(const char* const* filelist, bool isSave);

private:
    ApplicationInfo m_Info;
    bool m_Running = true;

    Timer m_Timer;

    SDL_Window* m_Window;
    TUnique<Renderer> m_Renderer;
    TUnique<StreamingManager> m_StreamingManager;
    Camera m_Camera;
    TUnique<JoltDebugRenderer> m_JoltDebugRenderer;

    Scene m_Scene;

    // Declaration order matters: ScriptSystem owns the live script objects and must be destroyed
    // before the engine that created them.
    TUnique<ScriptEngine> m_ScriptEngine;
    TUnique<ScriptSystem> m_ScriptSystem;

    EditorContext m_EditorContext;

    ViewportPanel m_ViewportPanel;
    OverlayPanel m_OverlayPanel;
    HierarchyPanel m_HierarchyPanel;
    InspectorPanel m_InspectorPanel;
    ContentDrawerPanel m_ContentDrawerPanel;
    RenderGraphPanel m_RenderGraphPanel;
    GpuTimingPanel m_GpuTimingPanel;
    CpuProfilerPanel m_CpuProfilerPanel;
    ConsolePanel m_ConsolePanel;
    SettingsPanel m_SettingsPanel;

    agfxDeviceInfo m_DeviceInfo;

    bool m_ShowSettingsPanel = false;

    std::mutex m_FileDialogMutex;
    String m_PendingDialogPath;
    bool m_PendingDialogIsSave = false;
    bool m_HasPendingDialogResult = false;

    // A dropped scene replaces every node, so it can't run from the drag-drop callback: that fires
    // mid-ImGui-frame, from panels still holding SceneNode* into the scene being destroyed.
    String m_PendingSceneLoadPath;
    bool m_HasPendingSceneLoad = false;
};
