/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 14:40:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/DebugRenderer.hpp>

/// @brief Exercises every DebugRenderer shape and option, as a live gallery plus an ImGui panel to
/// drive it. Purely a sample -- nothing else depends on it.
class DebugRendererDemo
{
public:
    /// Submits this frame's debug geometry. Call once per frame, before Renderer::Render().
    void Draw(float deltaTime);

    /// The "Debug Renderer" control panel. Call between ImGui::NewFrame() and ImGui::Render().
    void ShowUI();

    static constexpr uint32 kShapeCount = 10;

private:
    /// Draws shape `index` of the gallery centred on `position`, oriented by `rotation`.
    void DrawShape(DebugRenderer& debug, uint32 index, const glm::vec3& position, const glm::mat4& rotation, const DebugStyle& style);

    void DrawGallery(DebugRenderer& debug);
    void DrawDepthComparison(DebugRenderer& debug);
    void DrawRamps(DebugRenderer& debug);
    void DrawGroundGrid(DebugRenderer& debug);

    bool m_Enabled = true;
    bool m_Animate = true;
    float m_Time = 0.0f;

    bool m_ShowGallery = true;
    bool m_ShowDepthComparison = true;
    bool m_ShowRamps = true;
    bool m_ShowGroundGrid = true;

    // Gallery style. `m_ShowBoth` nests a shrunken filled copy inside each wireframe shape, which
    // is the most direct way to see both modes of every shape at once.
    bool m_Filled = false;
    bool m_ShowBoth = true;
    bool m_DepthTest = true;
    float m_Thickness = 2.0f;
    float m_Alpha = 1.0f;
    int m_Segments = 24;

    bool m_ShapeEnabled[kShapeCount] = { true, true, true, true, true, true, true, true, true, true };
};
