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

    void AddInstance(SceneNode* entity, const Instance& instance);
    void DeleteNode(SceneNode* node);

    void Reparent(SceneNode* node, SceneNode* newParent);

    void Update(StreamingManager& streamingManager);

    TArray<RenderInstance> BuildRenderInstances(StreamingManager& streamingManager);

    // Overrides are layered onto the pristine cooked material when GPUScene writes its material
    // buffer each frame -- nothing mutates CPUModel, so there is no "apply" step.
    MaterialOverride& GetOrCreateMaterialOverride(SceneNode& entity, int32 materialIndex);

    bool SaveToFile(const String& path) const;
    bool LoadFromFile(const String& path, StreamingManager& streamingManager);

    SceneNode& GetRoot() { return m_Root; }
    const SceneNode& GetRoot() const { return m_Root; }

private:
    void CollectRenderInstances(SceneNode& node, StreamingManager& streamingManager, TArray<RenderInstance>& out);
    void RemovePendingRequests(SceneNode& node);

    SceneNode m_Root;
    TDictionary<uint32, SceneNode*> m_PendingRequests;
    size_t m_ScannedModelCount = 0;
};
