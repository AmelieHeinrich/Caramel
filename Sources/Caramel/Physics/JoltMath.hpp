/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 15:01:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Math/Mat44.h>
#include <Jolt/Math/Vec3.h>

#include <glm/glm.hpp>

namespace Physics
{
    inline JPH::Vec3 ToJolt(const glm::vec3& v)
    {
        return JPH::Vec3(v.x, v.y, v.z);
    }

    inline JPH::Mat44 ToJolt(const glm::mat4& m)
    {
        return JPH::Mat44(
            JPH::Vec4(m[0].x, m[0].y, m[0].z, m[0].w),
            JPH::Vec4(m[1].x, m[1].y, m[1].z, m[1].w),
            JPH::Vec4(m[2].x, m[2].y, m[2].z, m[2].w),
            JPH::Vec4(m[3].x, m[3].y, m[3].z, m[3].w));
    }

    inline glm::vec3 ToGlm(JPH::Vec3Arg v)
    {
        return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    }
}
