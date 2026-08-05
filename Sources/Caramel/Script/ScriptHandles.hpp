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
// No default member initializers here: AngelScript's x64 MSVC calling convention glue treats a
// non-trivial default constructor as reason enough to return the type through a hidden pointer
// (COMPLEX_RETURN_MASK includes asOBJ_APP_CLASS_CONSTRUCTOR), even though the real ABI returns an
// 8-byte-or-smaller trivially-copyable struct like this one in a register. That mismatch silently
// shifts every argument of any native function returning one of these types by one slot. Every
// construction site already uses brace-init, which value-initializes (i.e. zeroes) omitted
// members, so trivial default construction changes no observable behavior.
struct EntityRef
{
    uint64 nodeId;
};

struct InstanceRef
{
    uint64 nodeId;
    uint32 index;
};

struct MeshRef
{
    uint64 nodeId;
    uint32 meshSlot;
};

struct MaterialRef
{
    uint64 nodeId;
    int32 materialIndex;

    // -1 (MaterialOverride::kAllMeshes) when the handle came from an Entity or EntityInstance: those
    // address the material as a whole. A handle obtained from a Mesh carries that mesh's slot, so a
    // mesh-scoped script only repaints its own mesh.
    int32 meshSlot;
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
