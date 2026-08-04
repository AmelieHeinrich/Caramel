/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 15:02:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "JoltDebugRenderer.hpp"
#include "JoltMath.hpp"

#include <Caramel/Renderer/DebugRenderer.hpp>

JoltDebugRenderer::JoltDebugRenderer()
{
    Initialize();
}

void JoltDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
{
    DebugStyle style;
    style.color = glm::vec4(inColor.r / 255.0f, inColor.g / 255.0f, inColor.b / 255.0f, inColor.a / 255.0f);
    style.depthTest = m_DepthTest;
    ::DebugRenderer::Get().Line(Physics::ToGlm(inFrom), Physics::ToGlm(inTo), style);
}

void JoltDebugRenderer::DrawText3D(JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float)
{
}
