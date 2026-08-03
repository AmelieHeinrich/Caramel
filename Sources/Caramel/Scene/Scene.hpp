/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:02:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Scene/SceneNode.hpp>
#include <Caramel/Scene/RenderInstance.hpp>

class StreamingManager;

// Owns the editor's scene hierarchy (folders + model entities, each with a list of instances) on
// top of StreamingManager's flat, per-mesh streaming list. Nothing here touches the GPU directly --
// it only resolves which StreamingModels belong to which entity (async, via request ids) and
// flattens (entity x instance x mesh) into a per-frame draw/pick list for the renderer/picker.
class Scene
{
public:
    SceneNode* CreateFolder(SceneNode* parent, const String& name);

    // Kicks off StreamingManager::LoadModel(cmdlPath); the entity's meshIndices populate
    // asynchronously as Update() discovers them. Starts with zero instances -- call AddInstance().
    SceneNode* CreateModelEntity(SceneNode* parent, const String& name, const String& cmdlPath, StreamingManager& streamingManager);

    void AddInstance(SceneNode* entity, const Instance& instance);
    void DeleteNode(SceneNode* node);

    // Moves node under newParent (nullptr = hierarchy root). No-ops if newParent isn't a Folder,
    // if node is already there, or if newParent is node itself or one of node's own descendants
    // (would create a cycle).
    void Reparent(SceneNode* node, SceneNode* newParent);

    // Call once per frame (after StreamingManager::Update()) to adopt newly-streamed meshes into
    // their owning entity's meshIndices.
    void Update(StreamingManager& streamingManager);

    TArray<RenderInstance> BuildRenderInstances(StreamingManager& streamingManager);

    // Finds entity's existing override for materialIndex, or appends and returns a fresh (all-fields-
    // disabled) one. Caller sets whichever override*/*Factor fields it wants, then must call
    // ApplyMaterialOverrides() to actually push the change into the live (streamed) material.
    MaterialOverride& GetOrCreateMaterialOverride(SceneNode& entity, int32 materialIndex);

    // Applies every entry in entity.materialOverrides onto its streamed model's materials, in place.
    // Safe/cheap to call repeatedly (e.g. once per new mesh discovered in Update(), and again after
    // any Inspector edit) -- a no-op until at least one of the entity's meshes has streamed in, since
    // that's what exposes the shared CPUModel's material array to mutate.
    void ApplyMaterialOverrides(SceneNode& entity, StreamingManager& streamingManager);

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
