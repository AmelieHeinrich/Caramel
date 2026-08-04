/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:01:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "SceneNode.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

glm::mat4 Instance::GetTransform() const
{
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotation = glm::eulerAngleXYZ(glm::radians(rotationEuler.x), glm::radians(rotationEuler.y), glm::radians(rotationEuler.z));
    glm::mat4 scaling = glm::scale(glm::mat4(1.0f), scale);
    return translation * rotation * scaling;
}

bool MeshTransform::IsIdentity() const
{
    return position == glm::vec3(0.0f) && rotationEuler == glm::vec3(0.0f) && scale == glm::vec3(1.0f);
}

glm::mat4 MeshTransform::GetTransform() const
{
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotation = glm::eulerAngleXYZ(glm::radians(rotationEuler.x), glm::radians(rotationEuler.y), glm::radians(rotationEuler.z));
    glm::mat4 scaling = glm::scale(glm::mat4(1.0f), scale);
    return translation * rotation * scaling;
}
