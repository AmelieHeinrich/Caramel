/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:00:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <glm/glm.hpp>

struct Instance
{
    String name;
    glm::vec3 position{ 0.0f };
    glm::vec3 rotationEuler{ 0.0f };
    glm::vec3 scale{ 1.0f };

    glm::mat4 GetTransform() const;
};

enum class ESceneNodeType
{
    Folder,
    Entity
};

struct MaterialOverride
{
    int32 materialIndex = -1;

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
    String name;
    ESceneNodeType type = ESceneNodeType::Folder;
    SceneNode* parent = nullptr;
    TArray<TUnique<SceneNode>> children;

    String cmdlPath;
    uint32 requestId = 0;
    TArray<uint32> meshIndices;
    TArray<Instance> instances;
    TArray<MaterialOverride> materialOverrides;
};
