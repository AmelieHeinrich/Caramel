/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:00:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <glm/glm.hpp>

// One placement of a model entity's meshes. rotationEuler is degrees, XYZ order (GetTransform()
// applies X, then Y, then Z).
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

// A scene-local edit to one of an entity's materials, keyed by materialIndex (the same raw index
// used by ModelMesh::materialIndex, scoped to this entity's own .cmdl). Applied on top of the
// asset's baked-in factors -- see Scene::ApplyMaterialOverrides -- so the same .cmdl loaded by a
// different entity/scene is never affected. Each factor is independently toggleable so an override
// can touch just e.g. metallic without also pinning base color to whatever it happened to be.
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
};

// A row in the scene hierarchy: either a pure grouping Folder, or a Model Entity (references one
// .cmdl asset and owns a list of placements). Owned by Scene via TUnique in the parent's children.
class SceneNode
{
public:
    String name;
    ESceneNodeType type = ESceneNodeType::Folder;
    SceneNode* parent = nullptr;
    TArray<TUnique<SceneNode>> children;

    // -- Entity-only fields (type == Entity) --
    String cmdlPath;
    uint32 requestId = 0;          // StreamingManager::LoadModel request id for cmdlPath
    TArray<uint32> meshIndices;    // indices into StreamingManager::GetModels(), discovered async
    TArray<Instance> instances;
    TArray<MaterialOverride> materialOverrides;
};
