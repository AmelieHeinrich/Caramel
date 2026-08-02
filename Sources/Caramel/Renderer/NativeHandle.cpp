/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 08:13:04
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "NativeHandle.hpp"

NativeHandle::NativeHandle(SDL_Window* window)
    : m_Window(window)
{
#if defined(CARAMEL_MACOS)
    m_MetalView = SDL_Metal_CreateView(m_Window);
#endif
}

NativeHandle::~NativeHandle()
{
#if defined(CARAMEL_MACOS)
    SDL_Metal_DestroyView(m_MetalView);
#endif
}

agfx::SwapChain NativeHandle::CreateSwapChain(agfx::Device& device,agfxSwapChainCreateInfo createInfo)
{
#if defined(CARAMEL_LINUX)
    agfxLinuxWindowHandle linuxWindowHandle = {};
    linuxWindowHandle.display = SDL_GetPointerProperty(SDL_GetWindowProperties(m_Window), SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    linuxWindowHandle.window = (uint64_t)(uintptr_t)SDL_GetPointerProperty(SDL_GetWindowProperties(m_Window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    createInfo.handle = &linuxWindowHandle;
#elif defined(CARAMEL_WINDOWS)
    createInfo.handle = SDL_GetPointerProperty(SDL_GetWindowProperties(m_Window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#elif defined(CARAMEL_MACOS)
    createInfo.handle = SDL_Metal_GetLayer(m_MetalView);
#endif

    return device.CreateSwapChain(createInfo);
}
