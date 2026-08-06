/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:00:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Script/ScriptTypes.hpp>

#include <glm/glm.hpp>

struct Instance
{
    String name;
    glm::vec3 position{ 0.0f };
    glm::vec3 rotationEuler{ 0.0f };
    glm::vec3 scale{ 1.0f };

    glm::mat4 GetTransform() const;
};

// Per-mesh offset applied on top of the instance transform, in the entity's local space. Meshes
// have no transform of their own otherwise -- the model's baked placement lives in StreamingModel
// and is shared by every entity using that model, so it cannot be edited per entity.
// Keyed by mesh slot (an index into SceneNode::meshIndices), so it is addressable from a MeshRef,
// which is what a mesh-scoped script holds. That also means the offset is shared by every instance
// of the node, exactly like a material override.
struct MeshTransform
{
    uint32 meshSlot = 0;

    glm::vec3 position{ 0.0f };
    glm::vec3 rotationEuler{ 0.0f };
    glm::vec3 scale{ 1.0f };

    bool IsIdentity() const;
    glm::mat4 GetTransform() const;
};

enum class ESceneNodeType
{
    Folder,
    Entity,
    Empty,
    Light
};

// A light node carries exactly one Instance, purely for its transform. That is what lets the
// inspector's transform section, the hierarchy row and Instance::GetTransform() work on it with no
// special case -- see SceneNodeTypeAllowsMultipleInstances for the part that is special.
inline bool SceneNodeTypeHasInstances(ESceneNodeType type)
{
    return type == ESceneNodeType::Entity || type == ESceneNodeType::Empty || type == ESceneNodeType::Light;
}

inline bool SceneNodeTypeCanParent(ESceneNodeType type)
{
    return type == ESceneNodeType::Folder || type == ESceneNodeType::Empty;
}

inline bool SceneNodeTypeAllowsMultipleInstances(ESceneNodeType type)
{
    return type == ESceneNodeType::Entity || type == ESceneNodeType::Empty;
}

enum class ELightType
{
    Directional,
    Point,
    Spot,
    Area
};

enum class EAreaShape
{
    Rect,
    Disk,
    Tube
};

// Authored light settings. Intensity is in photometric units that depend on the type -- the
// conversion to the radiometric value shaders consume happens once, in Scene::CollectLights.
struct LightComponent
{
    ELightType type = ELightType::Point;
    glm::vec3 color{ 1.0f };

    // Directional: lux. Point/Spot: lumens. Area: nits (cd/m^2).
    float32 intensity = 100000.0f;

    float32 range = 25.0f;
    float32 innerAngle = 20.0f;
    float32 outerAngle = 35.0f;

    EAreaShape shape = EAreaShape::Rect;

    // Rect: width/height. Disk: radius in .x. Tube: radius in .x, length in .y.
    glm::vec2 size{ 1.0f, 1.0f };
    bool twoSided = false;

    // Treats a point/spot light as a sphere rather than a singularity. Unused by the placeholder
    // shading loop, but soft shadows and ReSTIR both need it and it costs nothing to author now.
    float32 sourceRadius = 0.0f;

    bool enabled = true;
};

// The number in `intensity` means a different physical quantity per type, so carrying a value across
// a type change would read as 500 lux of daylight where 500 lumens was meant. Every site that changes
// a light's type resets the intensity through here.
//
// The lumen defaults look absurd next to a household bulb, and they are correct anyway: a point light
// loses its intensity to 1/d^2, so lighting a scene tens of metres across takes floodlight-class
// output to compete with even a dim overcast sky. 100k lm at 10 m lands in the same range as the 500
// lux directional; drop it to a 5000 lm bulb and the light genuinely does disappear under the ambient
// term. Anything that makes local lights look right at household lumen values is hiding the inverse
// square somewhere, and would have to be unhidden again for ReSTIR to sample correctly.
inline float32 LightDefaultIntensity(ELightType type)
{
    switch (type)
    {
        case ELightType::Directional: return 500.0f;      // lux, dim overcast
        case ELightType::Area:        return 1000.0f;     // nits
        default:                      return 100000.0f;   // lumens
    }
}

struct MaterialOverride
{
    int32 materialIndex = -1;

    // Which mesh slot of the entity this override applies to, or kAllMeshes for "every mesh using
    // this material". Meshes of one model routinely share a material index, so without this a
    // mesh-scoped edit (from the inspector or from a script holding a Mesh handle) leaked onto every
    // sibling mesh. Scenes saved before this existed have no meshSlot and load as kAllMeshes.
    static constexpr int32 kAllMeshes = -1;
    int32 meshSlot = kAllMeshes;

    bool overrideBaseColor = false;
    glm::vec4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };

    bool overrideMetallic = false;
    float32 metallicFactor = 1.0f;

    bool overrideRoughness = false;
    float32 roughnessFactor = 1.0f;

    bool overrideEmissive = false;
    glm::vec3 emissiveFactor{ 0.0f, 0.0f, 0.0f };

    // Material scheme selection. Empty means the default scheme (DefaultPBR).
    String schemeName;

    // Scheme parameter values keyed by parameter name -- deliberately not a packed blob, so that
    // reordering or adding a parameter in the scheme JSON cannot corrupt a saved scene. Scalars use
    // .x. Parameters absent here fall back to the scheme's declared default.
    TDictionary<String, glm::vec4> schemeParamValues;

    // A scheme assignment counts as an override on its own: without this the derived material slot
    // would never be allocated and the scheme would be ignored.
    bool HasAnyOverride() const
    {
        return overrideBaseColor || overrideMetallic || overrideRoughness || overrideEmissive
            || !schemeName.Empty() || schemeParamValues.Size() > 0;
    }

    // Runtime-only cache of this override's derived GPUScene material slot. Not serialized -- it is
    // rebuilt on demand, so keep it out of Scene::SaveToFile/LoadFromFile.
    uint32 gpuMaterialSlot = UINT32_MAX;
};

class SceneNode
{
public:
    // Stable identity handed to scripts. Monotonic and never reused, so a stale script handle can
    // never alias a different node. Not serialized -- assigned fresh on load.
    uint64 id = 0;

    String name;
    ESceneNodeType type = ESceneNodeType::Folder;
    SceneNode* parent = nullptr;
    TArray<TUnique<SceneNode>> children;

    String cmdlPath;
    uint32 requestId = 0;
    TArray<uint32> meshIndices;
    TArray<Instance> instances;
    TArray<MeshTransform> meshTransforms;
    TArray<MaterialOverride> materialOverrides;
    TArray<ScriptComponent> scripts;

    // Only meaningful when type == Light.
    LightComponent light;
};
