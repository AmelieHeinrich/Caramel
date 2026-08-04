/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 15:02:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

class JoltDebugRenderer final : public JPH::DebugRendererSimple
{
public:
    JoltDebugRenderer();

    void SetDepthTest(bool value) { m_DepthTest = value; }

    virtual void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
    virtual void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) override;

private:
    bool m_DepthTest = true;
};
