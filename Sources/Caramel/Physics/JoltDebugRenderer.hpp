/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 15:02:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

// Bridges Jolt's JPH::DebugRenderer interface -- the one Shape::Draw() and friends draw through --
// into Caramel's own DebugRenderer, so collider shapes render with the engine's existing
// wireframe/overlay pipeline instead of needing a second one. DebugRendererSimple supplies
// CreateTriangleBatch/DrawGeometry (and a DrawLine-based fallback for DrawTriangle), so only the
// two primitives below need implementing.
class JoltDebugRenderer final : public JPH::DebugRendererSimple
{
public:
    JoltDebugRenderer();

    // Depth-tested against the scene by default (so it reads as "this is where the collider
    // actually is"); set false for an always-on-top highlight, e.g. the picked instance.
    void SetDepthTest(bool value) { m_DepthTest = value; }

    virtual void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
    virtual void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) override;

private:
    bool m_DepthTest = true;
};
