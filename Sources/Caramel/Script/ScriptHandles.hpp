/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

class Scene;
class SceneNode;
class StreamingManager;
class StreamingModel;
struct Instance;
struct MaterialOverride;

// Script-visible handles are plain ids, never pointers: Instance lives inside a TArray that
// reallocates on AddInstance, and a SceneNode can be deleted while a script still holds a
// reference. Everything resolves through the bridge on every single access.
struct EntityRef
{
    uint64 nodeId = 0;
};

struct InstanceRef
{
    uint64 nodeId = 0;
    uint32 index = 0;
};

struct MeshRef
{
    uint64 nodeId = 0;
    uint32 meshSlot = 0;
};

struct MaterialRef
{
    uint64 nodeId = 0;
    int32 materialIndex = -1;
};

class ScriptSceneBridge
{
public:
    static void SetContext(Scene* scene, StreamingManager* streaming);
    static Scene* GetScene() { return s_Scene; }
    static StreamingManager* GetStreaming() { return s_Streaming; }

    static SceneNode* Resolve(const EntityRef& ref);
    static Instance* Resolve(const InstanceRef& ref);
    static StreamingModel* Resolve(const MeshRef& ref);
    static MaterialOverride* ResolveOrCreate(const MaterialRef& ref);

    static void QueueDestroy(uint64 nodeId);
    static TArray<uint64>& GetPendingDestroys() { return s_PendingDestroys; }
    static void ClearPendingDestroys() { s_PendingDestroys.Clear(); }

    static void NoteFailedResolve() { s_FailedResolves++; }
    static uint32 ConsumeFailedResolves();

private:
    static Scene* s_Scene;
    static StreamingManager* s_Streaming;
    static TArray<uint64> s_PendingDestroys;
    static uint32 s_FailedResolves;
};
