/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:57:20
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Application.hpp"

#include <Caramel/Core/Logger.hpp>

Application::Application(const ApplicationInfo& info)
    : m_Info(info)
{
    Logger::Initialize();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        CARAMEL_ERROR("SDL_Init failed: {}", SDL_GetError());
        assert(false);
    }

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

    m_Renderer = MakeUnique<Renderer>();
}

Application::~Application()
{
    SDL_DestroyWindow(m_Window);
    SDL_Quit();
}

void Application::Run()
{
    while (m_Running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                m_Running = false;
            }
        }
    }
}
