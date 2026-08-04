/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:02:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Scene/SceneNode.hpp>
#include <Caramel/Scene/RenderInstance.hpp>

class StreamingManager;

class Scene
{
public:
    SceneNode* CreateFolder(SceneNode* parent, const String& name);

    SceneNode* CreateModelEntity(SceneNode* parent, const String& name, const String& cmdlPath, StreamingManager& streamingManager);

    SceneNode* CreateEmptyEntity(SceneNode* parent, const String& name);

    void AddInstance(SceneNode* entity, const Instance& instance);
    void DeleteNode(SceneNode* node);

    void Reparent(SceneNode* node, SceneNode* newParent);

    void Update(StreamingManager& streamingManager);

    TArray<RenderInstance> BuildRenderInstances(StreamingManager& streamingManager);

    // Overrides are layered onto the pristine cooked material when GPUScene writes its material
    // buffer each frame -- nothing mutates CPUModel, so there is no "apply" step.
    // Per-mesh transform offsets. Absent slots render with no offset, so a scene that never touches
    // one costs nothing.
    MeshTransform& GetOrCreateMeshTransform(SceneNode& entity, uint32 meshSlot);
    const MeshTransform* FindMeshTransform(const SceneNode& entity, uint32 meshSlot) const;

    // meshSlot narrows the override to a single mesh of the entity; the default applies it to every
    // mesh using that material.
    MaterialOverride& GetOrCreateMaterialOverride(SceneNode& entity, int32 materialIndex, int32 meshSlot = MaterialOverride::kAllMeshes);

    // The override in effect for one mesh: an exact mesh-slot match wins over the whole-material
    // one, so a per-mesh edit shadows the entity-wide value instead of fighting it.
    const MaterialOverride* FindMaterialOverride(const SceneNode& entity, int32 materialIndex, int32 meshSlot) const;

    bool SaveToFile(const String& path) const;
    bool LoadFromFile(const String& path, StreamingManager& streamingManager);

    SceneNode& GetRoot() { return m_Root; }
    const SceneNode& GetRoot() const { return m_Root; }

    SceneNode* FindNodeById(uint64 id);
    const SceneNode* FindNodeById(uint64 id) const;

    SceneNode* FindNodeByName(const String& name);

private:
    void CollectRenderInstances(SceneNode& node, StreamingManager& streamingManager, TArray<RenderInstance>& out);
    void UnregisterSubtree(SceneNode& node);
    void RegisterNode(SceneNode* node);

    SceneNode m_Root;
    TDictionary<uint32, SceneNode*> m_PendingRequests;
    TDictionary<uint64, SceneNode*> m_NodesById;
    uint64 m_NextNodeId = 1;
    size_t m_ScannedModelCount = 0;
};
