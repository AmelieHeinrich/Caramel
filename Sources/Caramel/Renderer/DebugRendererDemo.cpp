/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 14:40:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "DebugRendererDemo.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

namespace
{
    // Gallery layout: a 5x2 grid of cells, sized to fit in the default camera's view.
    constexpr uint32 kColumns = 5;
    constexpr float kCellSpacing = 2.2f;
    constexpr float kRowSpacing = 2.4f;
    const glm::vec3 kGalleryOrigin(0.0f, 1.2f, 0.0f);

    const char* kShapeNames[DebugRendererDemo::kShapeCount] = {
        "Quad", "Arrow", "Triangle", "Cylinder", "Capsule",
        "Cone", "Sphere (rings)", "Sphere (triangulated)", "Axes", "Frustum",
    };

    // One hue per cell so neighbouring shapes stay distinguishable.
    const glm::vec3 kShapeColors[DebugRendererDemo::kShapeCount] = {
        { 0.95f, 0.60f, 0.25f }, { 0.40f, 0.85f, 0.95f }, { 0.95f, 0.35f, 0.45f },
        { 0.55f, 0.90f, 0.45f }, { 0.85f, 0.50f, 0.95f }, { 0.95f, 0.85f, 0.35f },
        { 0.45f, 0.65f, 0.95f }, { 0.95f, 0.45f, 0.75f }, { 0.85f, 0.85f, 0.85f },
        { 0.35f, 0.95f, 0.75f },
    };

    glm::vec3 CellPosition(uint32 index)
    {
        uint32 column = index % kColumns;
        uint32 row = index / kColumns;
        return kGalleryOrigin + glm::vec3(((float)column - (kColumns - 1) * 0.5f) * kCellSpacing,
                                          0.0f,
                                          -(float)row * kRowSpacing);
    }
}

void DebugRendererDemo::DrawShape(DebugRenderer& debug, uint32 index, const glm::vec3& position, const glm::mat4& rotation, const DebugStyle& style)
{
    // Every shape is authored to fit inside roughly a 0.8-radius sphere so grid cells don't collide.
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * rotation;
    glm::vec3 axisX = glm::vec3(transform[0]);
    glm::vec3 axisY = glm::vec3(transform[1]);
    glm::vec3 axisZ = glm::vec3(transform[2]);

    switch (index) {
        case 0: // Quad
            debug.Quad(transform, glm::vec2(0.8f, 0.6f), style);
            break;
        case 1: // Arrow
            debug.Arrow(position - axisY * 0.8f, position + axisY * 0.8f, style);
            break;
        case 2: // Triangle
            debug.Triangle(position + axisY * 0.8f,
                           position - axisY * 0.5f - axisX * 0.75f,
                           position - axisY * 0.5f + axisX * 0.75f, style);
            break;
        case 3: // Cylinder
            debug.Cylinder(position - axisY * 0.7f, axisY * 1.4f, 0.5f, style);
            break;
        case 4: // Capsule
            debug.Capsule(position - axisY * 0.4f, position + axisY * 0.4f, 0.4f, style);
            break;
        case 5: // Cone
            debug.Cone(position + axisY * 0.8f, -axisY, 1.6f, 0.55f, style);
            break;
        case 6: // Sphere (rings)
            debug.SphereRings(position, 0.75f, style);
            break;
        case 7: // Sphere (triangulated)
            debug.Sphere(position, 0.75f, style);
            break;
        case 8: // Axes -- ignores style.color by design.
            debug.Axes(transform, 0.8f, style);
            break;
        case 9: { // Frustum
            // A little camera sitting at the cell, looking along the cell's -Z; drawing its
            // view-projection is the same call site a real frustum-culling debug view would use.
            glm::mat4 view = glm::inverse(transform);
            glm::mat4 projection = glm::perspective(glm::radians(55.0f), 1.4f, 0.35f, 1.5f);
            debug.Frustum(projection * view, style);
            break;
        }
        default:
            break;
    }
    (void)axisZ;
}

void DebugRendererDemo::DrawGallery(DebugRenderer& debug)
{
    glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), m_Time * 0.5f, glm::normalize(glm::vec3(0.15f, 1.0f, 0.1f)));

    for (uint32 i = 0; i < kShapeCount; ++i) {
        if (!m_ShapeEnabled[i])
            continue;

        glm::vec3 position = CellPosition(i);

        DebugStyle style;
        style.color = glm::vec4(kShapeColors[i], m_Alpha);
        style.depthTest = m_DepthTest;
        style.thickness = m_Thickness;
        style.segments = (uint32)m_Segments;
        style.filled = m_Filled;

        DrawShape(debug, i, position, rotation, style);

        if (m_ShowBoth) {
            // Second pass in the opposite mode, scaled down so it nests inside the first instead
            // of z-fighting with it.
            style.filled = !m_Filled;
            style.color.a = style.filled ? m_Alpha * 0.55f : m_Alpha;
            DrawShape(debug, i, position, glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), glm::vec3(0.55f)), style);
        }
    }
}

void DebugRendererDemo::DrawDepthComparison(DebugRenderer& debug)
{
    // Two identical pairs pushed back into the scene: with Sponza loaded these end up inside the
    // geometry, so the depth-tested copy is clipped by the columns and the overlay copy isn't.
    for (int i = 0; i < 4; ++i) {
        glm::vec3 position(-4.5f + (float)i * 3.0f, 1.5f, -8.0f - (float)i * 3.0f);

        DebugStyle occluded;
        occluded.color = glm::vec4(0.3f, 0.9f, 1.0f, m_Alpha);
        occluded.depthTest = true;
        occluded.thickness = m_Thickness;
        occluded.segments = (uint32)m_Segments;
        debug.SphereRings(position, 0.9f, occluded);

        DebugStyle overlay;
        overlay.color = glm::vec4(1.0f, 0.4f, 0.2f, m_Alpha);
        overlay.depthTest = false;
        overlay.thickness = m_Thickness;
        overlay.segments = (uint32)m_Segments;
        debug.Axes(glm::translate(glm::mat4(1.0f), position), 1.3f, overlay);
        debug.Point(position, overlay);
    }
}

void DebugRendererDemo::DrawRamps(DebugRenderer& debug)
{
    // Thickness ramp: pixel widths 1..15 on identical world-space segments, so the only thing
    // varying on screen is the vertex-shader expansion width.
    for (int i = 0; i < 8; ++i) {
        DebugStyle style;
        style.color = glm::vec4(1.0f, 1.0f, 1.0f, m_Alpha);
        style.depthTest = m_DepthTest;
        style.thickness = 1.0f + (float)i * 2.0f;
        float x = -4.4f + (float)i * 1.25f;
        debug.Line(glm::vec3(x, -0.6f, 1.0f), glm::vec3(x + 0.9f, -0.6f, 1.0f), style);
    }

    // Point size ramp.
    for (int i = 0; i < 8; ++i) {
        DebugStyle style;
        style.color = glm::vec4(0.95f, 0.75f, 0.3f, m_Alpha);
        style.depthTest = m_DepthTest;
        style.thickness = 3.0f + (float)i * 4.0f;
        debug.Point(glm::vec3(-4.4f + (float)i * 1.25f, -1.2f, 1.0f), style);
    }

    // Alpha ramp on filled spheres.
    for (int i = 0; i < 6; ++i) {
        DebugStyle style;
        style.color = glm::vec4(0.45f, 0.85f, 1.0f, 0.1f + (float)i * 0.18f);
        style.depthTest = m_DepthTest;
        style.filled = true;
        style.segments = (uint32)m_Segments;
        debug.Sphere(glm::vec3(-4.0f + (float)i * 1.6f, -2.2f, 1.0f), 0.45f, style);
    }
}

void DebugRendererDemo::DrawGroundGrid(DebugRenderer& debug)
{
    constexpr int kHalfLines = 12;
    constexpr float kSpacing = 1.0f;
    constexpr float kExtent = kHalfLines * kSpacing;

    DebugStyle style;
    style.color = glm::vec4(0.45f, 0.42f, 0.38f, 0.5f);
    style.thickness = 1.0f;
    style.depthTest = true;

    DebugStyle axisStyle = style;
    axisStyle.color = glm::vec4(0.8f, 0.75f, 0.65f, 0.9f);
    axisStyle.thickness = 2.0f;

    for (int i = -kHalfLines; i <= kHalfLines; ++i) {
        float offset = (float)i * kSpacing;
        const DebugStyle& lineStyle = (i == 0) ? axisStyle : style;
        debug.Line(glm::vec3(offset, -3.0f, -kExtent), glm::vec3(offset, -3.0f, kExtent), lineStyle);
        debug.Line(glm::vec3(-kExtent, -3.0f, offset), glm::vec3(kExtent, -3.0f, offset), lineStyle);
    }
}

void DebugRendererDemo::Draw(float deltaTime)
{
    if (!m_Enabled)
        return;

    if (m_Animate)
        m_Time += deltaTime;

    DebugRenderer& debug = DebugRenderer::Get();

    if (m_ShowGroundGrid)
        DrawGroundGrid(debug);
    if (m_ShowGallery)
        DrawGallery(debug);
    if (m_ShowDepthComparison)
        DrawDepthComparison(debug);
    if (m_ShowRamps)
        DrawRamps(debug);
}

void DebugRendererDemo::ShowUI()
{
    ImGui::SetNextWindowSize(ImVec2(340, 520), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug Renderer");

    ImGui::Checkbox("Enabled", &m_Enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Animate", &m_Animate);

    ImGui::SeparatorText("Sections");
    ImGui::Checkbox("Shape gallery", &m_ShowGallery);
    ImGui::Checkbox("Depth-test comparison", &m_ShowDepthComparison);
    ImGui::Checkbox("Thickness / alpha ramps", &m_ShowRamps);
    ImGui::Checkbox("Ground grid", &m_ShowGroundGrid);

    ImGui::SeparatorText("Style");
    ImGui::Checkbox("Filled", &m_Filled);
    ImGui::SameLine();
    ImGui::Checkbox("Show both modes", &m_ShowBoth);
    ImGui::Checkbox("Depth tested", &m_DepthTest);
    ImGui::SliderFloat("Thickness (px)", &m_Thickness, 1.0f, 16.0f, "%.1f");
    ImGui::SliderFloat("Alpha", &m_Alpha, 0.05f, 1.0f, "%.2f");
    ImGui::SliderInt("Segments", &m_Segments, 3, 64);

    ImGui::SeparatorText("Shapes");
    if (ImGui::SmallButton("All")) {
        for (bool& enabled : m_ShapeEnabled)
            enabled = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        for (bool& enabled : m_ShapeEnabled)
            enabled = false;
    }
    // Listed in gallery order (left to right, then back a row), which doubles as the legend --
    // there is no text rendering in world space to label the cells with.
    for (uint32 i = 0; i < kShapeCount; ++i) {
        ImGui::PushID((int)i);
        ImGui::Checkbox(kShapeNames[i], &m_ShapeEnabled[i]);
        ImGui::SameLine();
        ImGui::ColorButton("##color", ImVec4(kShapeColors[i].r, kShapeColors[i].g, kShapeColors[i].b, 1.0f),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(14, 14));
        ImGui::PopID();
    }

    ImGui::SeparatorText("Submitted this frame");
    const DebugRenderer& debug = DebugRenderer::Get();
    ImGui::Text("%u segments, %u points, %u triangles",
                debug.GetSegmentCount(), debug.GetPointCount(), debug.GetTriangleCount());

    ImGui::End();
}
