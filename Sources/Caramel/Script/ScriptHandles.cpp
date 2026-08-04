/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Script/ScriptHandles.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Asset/StreamingManager.hpp>

Scene* ScriptSceneBridge::s_Scene = nullptr;
StreamingManager* ScriptSceneBridge::s_Streaming = nullptr;
TArray<uint64> ScriptSceneBridge::s_PendingDestroys;
uint32 ScriptSceneBridge::s_FailedResolves = 0;

void ScriptSceneBridge::SetContext(Scene* scene, StreamingManager* streaming)
{
    s_Scene = scene;
    s_Streaming = streaming;
    s_PendingDestroys.Clear();
    s_FailedResolves = 0;
}

SceneNode* ScriptSceneBridge::Resolve(const EntityRef& ref)
{
    if (!s_Scene || ref.nodeId == 0)
        return nullptr;

    SceneNode* node = s_Scene->FindNodeById(ref.nodeId);
    if (!node)
        NoteFailedResolve();
    return node;
}

Instance* ScriptSceneBridge::Resolve(const InstanceRef& ref)
{
    SceneNode* node = Resolve(EntityRef{ ref.nodeId });
    if (!node)
        return nullptr;

    if (ref.index >= node->instances.Size()) {
        NoteFailedResolve();
        return nullptr;
    }

    return &node->instances[ref.index];
}

StreamingModel* ScriptSceneBridge::Resolve(const MeshRef& ref)
{
    SceneNode* node = Resolve(EntityRef{ ref.nodeId });
    if (!node || !s_Streaming)
        return nullptr;

    if (ref.meshSlot >= node->meshIndices.Size()) {
        NoteFailedResolve();
        return nullptr;
    }

    const TArray<TShared<StreamingModel>>& models = s_Streaming->GetModels();
    uint32 meshIndex = node->meshIndices[ref.meshSlot];
    if (meshIndex >= models.Size()) {
        NoteFailedResolve();
        return nullptr;
    }

    return models[meshIndex].get();
}

MaterialOverride* ScriptSceneBridge::ResolveOrCreate(const MaterialRef& ref)
{
    SceneNode* node = Resolve(EntityRef{ ref.nodeId });
    if (!node || !s_Scene)
        return nullptr;

    return &s_Scene->GetOrCreateMaterialOverride(*node, ref.materialIndex, ref.meshSlot);
}

void ScriptSceneBridge::QueueDestroy(uint64 nodeId)
{
    if (nodeId == 0)
        return;

    for (uint64 pending : s_PendingDestroys) {
        if (pending == nodeId)
            return;
    }
    s_PendingDestroys.PushBack(nodeId);
}

uint32 ScriptSceneBridge::ConsumeFailedResolves()
{
    uint32 count = s_FailedResolves;
    s_FailedResolves = 0;
    return count;
}
