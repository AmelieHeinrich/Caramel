/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:20:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <glm/glm.hpp>

// Minimal free-fly viewport camera: WASD to move, hold the right mouse button and move the mouse
// to look around (mirrors common editor-viewport conventions so it doesn't fight ImGui's own use
// of the left mouse button for widget interaction).
class Camera
{
public:
    void Update(float deltaTime);

    glm::mat4 GetView() const;
    glm::mat4 GetProjection(float aspectRatio) const;
    glm::mat4 GetViewProjection(float aspectRatio) const { return GetProjection(aspectRatio) * GetView(); }

    const glm::vec3& GetPosition() const { return m_Position; }

private:
    glm::vec3 m_Position{ 0.0f, 2.0f, 5.0f };
    float m_Yaw = -90.0f;   // Degrees; -90 faces down -Z.
    float m_Pitch = 0.0f;

    float m_MoveSpeed = 5.0f;
    float m_LookSensitivity = 0.15f;

    float m_FovDegrees = 60.0f;
    float m_NearPlane = 0.1f;
    float m_FarPlane = 500.0f;
};
