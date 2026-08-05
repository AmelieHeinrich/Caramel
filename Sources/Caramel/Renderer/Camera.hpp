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
    float GetNearPlane() const { return m_NearPlane; }
    float GetFarPlane() const { return m_FarPlane; }

    // Extracts the 6 view-frustum planes (xyz = normal, w = distance, normalized) from the
    // view-projection matrix via the Gribb-Hartmann method, in Left/Right/Bottom/Top/Near/Far order.
    void GetFrustumPlanes(float aspectRatio, glm::vec4 outPlanes[6]) const;

    void ScreenPointToRay(const glm::vec2& screenPos, uint32 width, uint32 height, float aspectRatio, glm::vec3& outOrigin, glm::vec3& outDir) const;

private:
    glm::vec3 m_Position{ 0.0f, 2.0f, 5.0f };
    float m_Yaw = -90.0f;
    float m_Pitch = 0.0f;

    // Current world-space velocity, eased toward the WASDQE-driven target velocity every frame
    // (see Update) instead of snapping to it -- gives movement weight without adding input lag.
    glm::vec3 m_Velocity{ 0.0f };

    float m_MoveSpeed = 5.0f;
    float m_SprintMultiplier = 3.0f; // held LShift/RShift
    float m_Acceleration = 20.0f; // 1/s, how fast m_Velocity closes in on its target while speeding up
    float m_Deceleration = 12.0f; // 1/s, same but while slowing toward zero -- slightly floatier stop
    float m_LookSensitivity = 0.15f;
    float m_ZoomSpeed = 1.5f; // world units per scroll notch, dollied along the view direction

    float m_FovDegrees = 60.0f;
    float m_NearPlane = 0.1f;
    float m_FarPlane = 500.0f;
};
