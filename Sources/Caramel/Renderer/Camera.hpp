/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:20:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <glm/glm.hpp>

class Camera
{
public:
    void Update(float deltaTime);

    glm::mat4 GetView() const;
    glm::mat4 GetProjection(float aspectRatio) const;
    glm::mat4 GetViewProjection(float aspectRatio) const { return GetProjection(aspectRatio) * GetView(); }

    const glm::vec3& GetPosition() const { return m_Position; }

    void ScreenPointToRay(const glm::vec2& screenPos, uint32 width, uint32 height, float aspectRatio, glm::vec3& outOrigin, glm::vec3& outDir) const;

private:
    glm::vec3 m_Position{ 0.0f, 2.0f, 5.0f };
    float m_Yaw = -90.0f;
    float m_Pitch = 0.0f;

    float m_MoveSpeed = 5.0f;
    float m_LookSensitivity = 0.15f;

    float m_FovDegrees = 60.0f;
    float m_NearPlane = 0.1f;
    float m_FarPlane = 500.0f;
};
