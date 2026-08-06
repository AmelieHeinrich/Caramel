/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Scene/SceneNode.hpp>

#include <glm/glm.hpp>

/// @brief Photographic exposure, 1 / (1.2 * 2^EV100) at EV100 = 8, folded into every light's
/// intensity at gather time.
///
/// It has to happen somewhere: Composite.hlsl applies a gamma curve and nothing else -- there is no
/// tonemapper and no exposure yet (both are on Notes/TODO.md) -- so anything above 1.0 clips flat
/// white. Real photometric values do not survive that; a 5000 lm bulb is 398 candela. EV100 = 8 is an
/// interior/dusk exposure, picked because it is where lux, lumens and nits all land in the same
/// usable range at once, near the 4.25 the placeholder sun used to produce.
///
/// When the physically based camera or auto-exposure lands, this multiply leaves the gather and
/// becomes a per-frame value; nothing else about the pipeline changes.
constexpr float32 kLightExposure = 1.0f / (1.2f * 256.0f);

/// @brief One light of the scene, resolved into world space with its photometric intensity already
/// converted to the radiometric value shaders consume.
///
/// The CPU-side counterpart of GPULight, and the thing the editor gizmo reads: resolving once here
/// means the gizmo and the shader can never disagree about where a light points or how far it
/// reaches.
struct SceneLight
{
    ELightType type = ELightType::Point;
    EAreaShape shape = EAreaShape::Rect;

    glm::vec3 position{ 0.0f };

    // The direction the light points, i.e. the transform's -Z. glTF's convention, and models come
    // from glTF, so a light imported alongside one aims the same way.
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };

    // Area basis, scaled by the light's transform. Unused by the other three types.
    glm::vec3 right{ 1.0f, 0.0f, 0.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };

    glm::vec3 color{ 1.0f };

    // Exposed lux (Directional) or candela (everything else -- an area light's nits are integrated
    // over its emissive area here, so treating it as punctual is correct in the far field).
    float32 intensity = 0.0f;

    // Zero means unbounded, which is only ever a directional light. Every other type has a real
    // bounding sphere here, which is what clustered culling and ReSTIR reservoir sampling need.
    float32 range = 0.0f;

    // Cone falloff as saturate(cosAngle * scale + offset), precomputed so the shader is one madd.
    float32 spotScale = 0.0f;
    float32 spotOffset = 0.0f;

    // Kept in degrees for the editor gizmo, which draws an actual cone.
    float32 outerAngle = 0.0f;

    glm::vec2 size{ 0.0f };
    float32 sourceRadius = 0.0f;
    bool twoSided = false;

    // Which node this came from, so the editor can find the light belonging to the selection.
    const SceneNode* owner = nullptr;
};
