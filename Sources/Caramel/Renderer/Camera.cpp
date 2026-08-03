/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:20:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Camera.hpp"

#include <Caramel/Core/Input.hpp>

#include <glm/gtc/matrix_transform.hpp>

void Camera::Update(float deltaTime)
{
    glm::vec3 forward(
        cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch)),
        sin(glm::radians(m_Pitch)),
        sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch)));
    forward = glm::normalize(forward);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    if (Input::IsMouseButtonDown(SDL_BUTTON_RIGHT))
    {
        glm::vec2 mouseDelta = Input::GetMouseDelta();
        m_Yaw += mouseDelta.x * m_LookSensitivity;
        m_Pitch -= mouseDelta.y * m_LookSensitivity;
        m_Pitch = glm::clamp(m_Pitch, -89.0f, 89.0f);

        float speed = m_MoveSpeed * deltaTime;
        if (Input::IsKeyDown(SDL_SCANCODE_W)) m_Position += forward * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_S)) m_Position -= forward * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_D)) m_Position += right * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_A)) m_Position -= right * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_E)) m_Position += up * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_Q)) m_Position -= up * speed;
    }
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

void Camera::ScreenPointToRay(const glm::vec2& screenPos, uint32 width, uint32 height, float aspectRatio, glm::vec3& outOrigin, glm::vec3& outDir) const
{
    // Same NDC-depth convention as DebugRenderer::Frustum, which unprojects the same way.
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
