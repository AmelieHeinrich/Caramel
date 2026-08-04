/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>

class Application;
class Scene;
class SceneNode;
class StreamingModel;

struct EditorContext
{
    Scene& CurrentScene;
    SDL_Window* Window = nullptr;
    Application* Owner = nullptr;

    SceneNode* SelectedEntity = nullptr;
    uint32 SelectedInstance = 0;
    StreamingModel* SelectedMesh = nullptr;

    ImVec2 ViewportRectMin{ 0.0f, 0.0f };
    ImVec2 ViewportRectSize{ 1.0f, 1.0f };
    bool ViewportHovered = false;
};
