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

#include <SDL3/SDL.h>

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
    void ShowContentViewer();
    void ShowModelViewer();
    void ShowViewport();
    void ShowInspector();
    void ShowRendererSettings();

private:
    ApplicationInfo m_Info;
    bool m_Running = true;

    SDL_Window* m_Window;
    TUnique<Renderer> m_Renderer;
    TUnique<StreamingManager> m_StreamingManager;
    Camera m_Camera;

    agfxDeviceInfo m_DeviceInfo;

    // Content Viewer state.
    bool m_ContentViewerLoaded = false;
    bool m_ContentViewerAutoStream = true;
    float m_ContentViewerStreamInterval = 0.5f;
    float m_ContentViewerStreamTimer = 0.0f;

    // Model Viewer state.
    bool m_ModelViewerLoaded = false;
    bool m_ModelViewerAutoStream = true;
    float m_ModelViewerStreamInterval = 0.5f;
    float m_ModelViewerStreamTimer = 0.0f;
};
