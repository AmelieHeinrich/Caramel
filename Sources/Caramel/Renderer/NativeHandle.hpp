/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 08:07:51
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <AGFX/agfx.hpp>

#include <SDL3/SDL.h>
#if defined(CARAMEL_MACOS)
    #include <SDL3/SDL_metal.h>
#endif

class NativeHandle
{
public:
    NativeHandle(SDL_Window* window);
    ~NativeHandle();

    agfx::SwapChain CreateSwapChain(agfx::Device& device, agfxSwapChainCreateInfo createInfo);
private:
    SDL_Window* m_Window;

#if defined(CARAMEL_MACOS)
    SDL_MetalView m_MetalView;
#endif
};
