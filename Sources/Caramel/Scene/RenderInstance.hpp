/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <glm/glm.hpp>

class SceneNode;
class StreamingModel;

struct RenderInstance
{
    StreamingModel* mesh = nullptr;
    glm::mat4 transform{ 1.0f };
    SceneNode* owner = nullptr;
    uint32 instanceIndex = 0;
};
