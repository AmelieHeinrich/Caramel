/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:20:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Camera.hpp"

#include <Caramel/Core/Input.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>

#include <cmath>

void Camera::Update(float deltaTime)
{
    // Look-drag still gates on holding the mouse (so panels/UI clicks elsewhere in the editor don't
    // spin the camera), but movement below no longer does -- only the drag itself needs the OS
    // cursor hidden and unbounded, so the relative-mode toggle stays tied to this edge, not to WASD.
    if (Input::IsMouseButtonPressed(SDL_BUTTON_RIGHT))
        Input::SetRelativeMouseMode(true);
    if (Input::IsMouseButtonReleased(SDL_BUTTON_RIGHT))
        Input::SetRelativeMouseMode(false);

    if (Input::IsMouseButtonDown(SDL_BUTTON_RIGHT))
    {
        glm::vec2 mouseDelta = Input::GetMouseDelta();
        m_Yaw += mouseDelta.x * m_LookSensitivity;
        m_Pitch -= mouseDelta.y * m_LookSensitivity;
        m_Pitch = glm::clamp(m_Pitch, -89.0f, 89.0f);
    }

    glm::vec3 forward(
        cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch)),
        sin(glm::radians(m_Pitch)),
        sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch)));
    forward = glm::normalize(forward);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    glm::vec3 inputDir(0.0f);
    if (Input::IsKeyDown(SDL_SCANCODE_W)) inputDir += forward;
    if (Input::IsKeyDown(SDL_SCANCODE_S)) inputDir -= forward;
    if (Input::IsKeyDown(SDL_SCANCODE_D)) inputDir += right;
    if (Input::IsKeyDown(SDL_SCANCODE_A)) inputDir -= right;
    if (Input::IsKeyDown(SDL_SCANCODE_E)) inputDir += up;
    if (Input::IsKeyDown(SDL_SCANCODE_Q)) inputDir -= up;
    if (glm::dot(inputDir, inputDir) > 0.0f)
        inputDir = glm::normalize(inputDir);

    float speed = m_MoveSpeed;
    if (Input::IsKeyDown(SDL_SCANCODE_LSHIFT) || Input::IsKeyDown(SDL_SCANCODE_RSHIFT))
        speed *= m_SprintMultiplier;

    glm::vec3 targetVelocity = inputDir * speed;

    // Frame-rate-independent exponential ease toward the target velocity: whichever rate applies,
    // (1 - e^(-rate*dt)) is the fraction of the remaining gap closed this frame, so the same rate
    // produces the same feel at any frame time instead of a fixed per-frame step would.
    float rate = glm::length(targetVelocity) > glm::length(m_Velocity) ? m_Acceleration : m_Deceleration;
    m_Velocity = glm::mix(m_Velocity, targetVelocity, 1.0f - std::exp(-rate * deltaTime));

    m_Position += m_Velocity * deltaTime;

    // Zoom: scroll wheel dollies along the view direction regardless of whether the look-drag is
    // held, for a quick way to approach/back off from whatever's in frame without full WASD flying.
    float scroll = Input::GetMouseScrollDelta().y;
    if (scroll != 0.0f)
        m_Position += forward * scroll * m_ZoomSpeed;
}

glm::mat4 Camera::GetView() const
{
    glm::vec3 forward(
        cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch)),
        sin(glm::radians(m_Pitch)),
        sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch)));
    return glm::lookAt(m_Position, m_Position + glm::normalize(forward), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjection(float aspectRatio) const
{
    glm::mat4 projection = glm::perspective(glm::radians(m_FovDegrees), aspectRatio, m_NearPlane, m_FarPlane);
    return projection;
}

void Camera::GetFrustumPlanes(float aspectRatio, glm::vec4 outPlanes[6]) const
{
    glm::mat4 viewProj = GetViewProjection(aspectRatio);

    glm::vec4 row0 = glm::row(viewProj, 0);
    glm::vec4 row1 = glm::row(viewProj, 1);
    glm::vec4 row2 = glm::row(viewProj, 2);
    glm::vec4 row3 = glm::row(viewProj, 3);

    outPlanes[0] = row3 + row0; // Left
    outPlanes[1] = row3 - row0; // Right
    outPlanes[2] = row3 + row1; // Bottom
    outPlanes[3] = row3 - row1; // Top
#if defined(GLM_FORCE_DEPTH_ZERO_TO_ONE)
    outPlanes[4] = row2;        // Near
#else
    outPlanes[4] = row3 + row2; // Near
#endif
    outPlanes[5] = row3 - row2; // Far

    for (uint32 i = 0; i < 6; ++i)
    {
        float length = glm::length(glm::vec3(outPlanes[i]));
        if (length > 0.0f)
            outPlanes[i] /= length;
    }
}

void Camera::ScreenPointToRay(const glm::vec2& screenPos, uint32 width, uint32 height, float aspectRatio, glm::vec3& outOrigin, glm::vec3& outDir) const
{
#if defined(GLM_FORCE_DEPTH_ZERO_TO_ONE)
    constexpr float kNdcNearZ = 0.0f;
#else
    constexpr float kNdcNearZ = -1.0f;
#endif

    glm::mat4 inverseViewProj = glm::inverse(GetViewProjection(aspectRatio));

    float ndcX = width != 0 ? (2.0f * screenPos.x / (float)width - 1.0f) : 0.0f;
    float ndcY = height != 0 ? (1.0f - 2.0f * screenPos.y / (float)height) : 0.0f;

    glm::vec4 nearPoint = inverseViewProj * glm::vec4(ndcX, ndcY, kNdcNearZ, 1.0f);
    glm::vec4 farPoint = inverseViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

    outOrigin = glm::vec3(nearPoint) / nearPoint.w;
    outDir = (glm::vec3(farPoint) / farPoint.w) - outOrigin;
}
