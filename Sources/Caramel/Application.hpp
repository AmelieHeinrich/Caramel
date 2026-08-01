/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:48:03
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Renderer.hpp>

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
    ApplicationInfo m_Info;
    bool m_Running = true;

    SDL_Window* m_Window;
    TUnique<Renderer> m_Renderer;
};
