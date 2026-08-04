/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:01:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ViewportPanel.hpp"

#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Renderer/Renderer.hpp>

#include <imgui.h>
#include <FontAwesome/FA.h>

namespace
{
    constexpr const char* kContentFileDragDropID = "CONTENT_FILE_PATH";
}

const char* const ViewportPanel::kTitle = ICON_FA_CUBE " Viewport";

void ViewportPanel::Draw(EditorContext& context, Renderer& renderer)
{
    ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(kTitle, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    context.ViewportHovered = ImGui::IsWindowHovered();

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    context.ViewportRectMin = ImGui::GetCursorScreenPos();
    context.ViewportRectSize = ImVec2(contentSize.x > 0.0f ? contentSize.x : 1.0f, contentSize.y > 0.0f ? contentSize.y : 1.0f);

    int32 windowWidth, windowHeight;
    int32 windowPixelWidth, windowPixelHeight;
    SDL_GetWindowSize(context.Window, &windowWidth, &windowHeight);
    SDL_GetWindowSizeInPixels(context.Window, &windowPixelWidth, &windowPixelHeight);
    float dpiScale = (float)windowPixelWidth / (float)windowWidth;
    
    context.ViewportRectSize.x *= dpiScale;
    context.ViewportRectSize.y *= dpiScale;

    renderer.SetViewportSize((uint32)context.ViewportRectSize.x, (uint32)context.ViewportRectSize.y);
    ImGui::Image(renderer.GetViewportTextureID(), ImVec2(context.ViewportRectSize.x / dpiScale, context.ViewportRectSize.y / dpiScale));

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(kContentFileDragDropID)) {
            if (m_OnDropFile)
                m_OnDropFile(String((const char*)accepted->Data));
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}
