/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Script/ScriptBindings.hpp>
#include <Caramel/Script/ScriptHandles.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Asset/StreamingManager.hpp>

#include <glm/glm.hpp>

#include <string>

namespace
{
    bool Entity_IsValid(const EntityRef* self)
    {
        return ScriptSceneBridge::Resolve(*self) != nullptr;
    }

    uint32 Entity_GetInstanceCount(const EntityRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(*self);
        return node ? (uint32)node->instances.Size() : 0;
    }

    InstanceRef Entity_GetInstance(uint32 index, const EntityRef* self)
    {
        return InstanceRef{ self->nodeId, index };
    }

    InstanceRef Entity_AddInstance(const glm::vec3& position, const EntityRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(*self);
        Scene* scene = ScriptSceneBridge::GetScene();
        if (!node || !scene)
            return InstanceRef{};

        Instance instance;
        instance.position = position;
        scene->AddInstance(node, instance);

        return InstanceRef{ self->nodeId, (uint32)node->instances.Size() - 1 };
    }

    uint32 Entity_GetMeshCount(const EntityRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(*self);
        return node ? (uint32)node->meshIndices.Size() : 0;
    }

    MeshRef Entity_GetMesh(uint32 slot, const EntityRef* self)
    {
        return MeshRef{ self->nodeId, slot };
    }

    MaterialRef Entity_GetMaterial(int32 materialIndex, const EntityRef* self)
    {
        return MaterialRef{ self->nodeId, materialIndex, MaterialOverride::kAllMeshes };
    }

    std::string Entity_GetName(const EntityRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(*self);
        return node ? std::string(node->name.CStr()) : std::string();
    }

    void Entity_SetName(const std::string& name, const EntityRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(*self);
        if (node)
            node->name = String(name);
    }

    void Entity_Destroy(const EntityRef* self)
    {
        ScriptSceneBridge::QueueDestroy(self->nodeId);
    }

    bool Instance_IsValid(const InstanceRef* self)
    {
        return ScriptSceneBridge::Resolve(*self) != nullptr;
    }

    glm::vec3 Instance_GetPosition(const InstanceRef* self)
    {
        Instance* instance = ScriptSceneBridge::Resolve(*self);
        return instance ? instance->position : glm::vec3(0.0f);
    }

    void Instance_SetPosition(const glm::vec3& value, const InstanceRef* self)
    {
        Instance* instance = ScriptSceneBridge::Resolve(*self);
        if (instance)
        {
            instance->position = value;
            if (Scene* scene = ScriptSceneBridge::GetScene())
                scene->MarkRenderInstancesDirty();
        }
    }

    glm::vec3 Instance_GetRotation(const InstanceRef* self)
    {
        Instance* instance = ScriptSceneBridge::Resolve(*self);
        return instance ? instance->rotationEuler : glm::vec3(0.0f);
    }

    void Instance_SetRotation(const glm::vec3& value, const InstanceRef* self)
    {
        Instance* instance = ScriptSceneBridge::Resolve(*self);
        if (instance)
        {
            instance->rotationEuler = value;
            if (Scene* scene = ScriptSceneBridge::GetScene())
                scene->MarkRenderInstancesDirty();
        }
    }

    glm::vec3 Instance_GetScale(const InstanceRef* self)
    {
        Instance* instance = ScriptSceneBridge::Resolve(*self);
        return instance ? instance->scale : glm::vec3(1.0f);
    }

    void Instance_SetScale(const glm::vec3& value, const InstanceRef* self)
    {
        Instance* instance = ScriptSceneBridge::Resolve(*self);
        if (instance)
        {
            instance->scale = value;
            if (Scene* scene = ScriptSceneBridge::GetScene())
                scene->MarkRenderInstancesDirty();
        }
    }

    glm::mat4 Instance_GetTransform(const InstanceRef* self)
    {
        Instance* instance = ScriptSceneBridge::Resolve(*self);
        return instance ? instance->GetTransform() : glm::mat4(1.0f);
    }

    EntityRef Instance_GetEntity(const InstanceRef* self)
    {
        return EntityRef{ self->nodeId };
    }

    MaterialRef Instance_GetMaterial(int32 materialIndex, const InstanceRef* self)
    {
        return MaterialRef{ self->nodeId, materialIndex, MaterialOverride::kAllMeshes };
    }

    // Reads go through the stored offset when there is one so an unset mesh reports identity rather
    // than allocating a MeshTransform just to be looked at.
    const MeshTransform* Mesh_FindTransform(const MeshRef* self)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        SceneNode* node = ScriptSceneBridge::Resolve(EntityRef{ self->nodeId });
        if (!scene || !node)
            return nullptr;
        return scene->FindMeshTransform(*node, self->meshSlot);
    }

    MeshTransform* Mesh_GetOrCreateTransform(const MeshRef* self)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        SceneNode* node = ScriptSceneBridge::Resolve(EntityRef{ self->nodeId });
        if (!scene || !node || self->meshSlot >= node->meshIndices.Size())
            return nullptr;
        return &scene->GetOrCreateMeshTransform(*node, self->meshSlot);
    }

    glm::vec3 Mesh_GetPosition(const MeshRef* self)
    {
        const MeshTransform* meshTransform = Mesh_FindTransform(self);
        return meshTransform ? meshTransform->position : glm::vec3(0.0f);
    }

    void Mesh_SetPosition(const glm::vec3& value, const MeshRef* self)
    {
        if (MeshTransform* meshTransform = Mesh_GetOrCreateTransform(self))
            meshTransform->position = value;
    }

    glm::vec3 Mesh_GetRotation(const MeshRef* self)
    {
        const MeshTransform* meshTransform = Mesh_FindTransform(self);
        return meshTransform ? meshTransform->rotationEuler : glm::vec3(0.0f);
    }

    void Mesh_SetRotation(const glm::vec3& value, const MeshRef* self)
    {
        if (MeshTransform* meshTransform = Mesh_GetOrCreateTransform(self))
            meshTransform->rotationEuler = value;
    }

    glm::vec3 Mesh_GetScale(const MeshRef* self)
    {
        const MeshTransform* meshTransform = Mesh_FindTransform(self);
        return meshTransform ? meshTransform->scale : glm::vec3(1.0f);
    }

    void Mesh_SetScale(const glm::vec3& value, const MeshRef* self)
    {
        if (MeshTransform* meshTransform = Mesh_GetOrCreateTransform(self))
            meshTransform->scale = value;
    }

    bool Mesh_IsValid(const MeshRef* self)
    {
        return ScriptSceneBridge::Resolve(*self) != nullptr;
    }

    std::string Mesh_GetName(const MeshRef* self)
    {
        StreamingModel* mesh = ScriptSceneBridge::Resolve(*self);
        return mesh ? std::string(mesh->GetMesh().name.CStr()) : std::string();
    }

    glm::vec3 Mesh_GetBoundsMin(const MeshRef* self)
    {
        StreamingModel* mesh = ScriptSceneBridge::Resolve(*self);
        return mesh ? mesh->GetMesh().boundsMin : glm::vec3(0.0f);
    }

    glm::vec3 Mesh_GetBoundsMax(const MeshRef* self)
    {
        StreamingModel* mesh = ScriptSceneBridge::Resolve(*self);
        return mesh ? mesh->GetMesh().boundsMax : glm::vec3(0.0f);
    }

    EntityRef Mesh_GetEntity(const MeshRef* self)
    {
        return EntityRef{ self->nodeId };
    }

    MaterialRef Mesh_GetMaterial(const MeshRef* self)
    {
        StreamingModel* mesh = ScriptSceneBridge::Resolve(*self);
        if (!mesh)
            return MaterialRef{ self->nodeId, -1, MaterialOverride::kAllMeshes };
        return MaterialRef{ self->nodeId, mesh->GetMesh().materialIndex, (int32)self->meshSlot };
    }

    bool Material_IsValid(const MaterialRef* self)
    {
        return ScriptSceneBridge::Resolve(EntityRef{ self->nodeId }) != nullptr;
    }

    void Material_SetBaseColor(const glm::vec4& value, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (!matOverride)
            return;
        matOverride->overrideBaseColor = true;
        matOverride->baseColorFactor = value;
    }

    void Material_SetMetallic(float32 value, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (!matOverride)
            return;
        matOverride->overrideMetallic = true;
        matOverride->metallicFactor = value;
    }

    void Material_SetRoughness(float32 value, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (!matOverride)
            return;
        matOverride->overrideRoughness = true;
        matOverride->roughnessFactor = value;
    }

    void Material_SetEmissive(const glm::vec3& value, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (!matOverride)
            return;
        matOverride->overrideEmissive = true;
        matOverride->emissiveFactor = value;
    }

    void Material_ClearOverrides(const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (!matOverride)
            return;
        matOverride->overrideBaseColor = false;
        matOverride->overrideMetallic = false;
        matOverride->overrideRoughness = false;
        matOverride->overrideEmissive = false;
    }

    void Material_SetScheme(const std::string& schemeName, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (matOverride)
            matOverride->schemeName = String(schemeName);
    }

    void Material_SetParamFloat(const std::string& name, float32 value, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (matOverride)
            matOverride->schemeParamValues[String(name)] = glm::vec4(value, 0.0f, 0.0f, 0.0f);
    }

    void Material_SetParamVec3(const std::string& name, const glm::vec3& value, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (matOverride)
            matOverride->schemeParamValues[String(name)] = glm::vec4(value, 0.0f);
    }

    void Material_SetParamVec4(const std::string& name, const glm::vec4& value, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (matOverride)
            matOverride->schemeParamValues[String(name)] = value;
    }

    float32 Material_GetParamFloat(const std::string& name, const MaterialRef* self)
    {
        MaterialOverride* matOverride = ScriptSceneBridge::ResolveOrCreate(*self);
        if (!matOverride)
            return 0.0f;

        auto it = matOverride->schemeParamValues.Find(String(name));
        return it != matOverride->schemeParamValues.End() ? it->second.x : 0.0f;
    }

    EntityRef Scene_SpawnModel(const std::string& cmdlPath, const std::string& name)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        StreamingManager* streaming = ScriptSceneBridge::GetStreaming();
        if (!scene || !streaming)
            return EntityRef{};

        SceneNode* node = scene->CreateModelEntity(nullptr, String(name), String(cmdlPath), *streaming);
        if (!node)
            return EntityRef{};

        scene->AddInstance(node, Instance());
        return EntityRef{ node->id };
    }

    EntityRef Scene_SpawnEmpty(const std::string& name)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        if (!scene)
            return EntityRef{};

        SceneNode* node = scene->CreateEmptyEntity(nullptr, String(name));
        return node ? EntityRef{ node->id } : EntityRef{};
    }

    EntityRef Scene_CreateFolder(const std::string& name)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        if (!scene)
            return EntityRef{};

        SceneNode* node = scene->CreateFolder(nullptr, String(name));
        return node ? EntityRef{ node->id } : EntityRef{};
    }

    EntityRef Scene_FindEntity(const std::string& name)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        if (!scene)
            return EntityRef{};

        SceneNode* node = scene->FindNodeByName(String(name));
        return node ? EntityRef{ node->id } : EntityRef{};
    }

    void Scene_DestroyEntity(const EntityRef& ref)
    {
        ScriptSceneBridge::QueueDestroy(ref.nodeId);
    }

    void Scene_Reparent(const EntityRef& child, const EntityRef& newParent)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        if (!scene)
            return;

        SceneNode* childNode = ScriptSceneBridge::Resolve(child);
        SceneNode* parentNode = ScriptSceneBridge::Resolve(newParent);
        if (childNode && parentNode)
            scene->Reparent(childNode, parentNode);
    }
}

void RegisterSceneAPI(asIScriptEngine* engine)
{
    const asDWORD handleFlags = asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLINTS;

    engine->RegisterObjectType("Entity", sizeof(EntityRef), handleFlags | asGetTypeTraits<EntityRef>());
    engine->RegisterObjectType("EntityInstance", sizeof(InstanceRef), handleFlags | asGetTypeTraits<InstanceRef>());
    engine->RegisterObjectType("Mesh", sizeof(MeshRef), handleFlags | asGetTypeTraits<MeshRef>());
    engine->RegisterObjectType("Material", sizeof(MaterialRef), handleFlags | asGetTypeTraits<MaterialRef>());

    engine->RegisterObjectMethod("Entity", "bool IsValid() const", asFUNCTION(Entity_IsValid), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "uint GetInstanceCount() const", asFUNCTION(Entity_GetInstanceCount), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "EntityInstance GetInstance(uint) const", asFUNCTION(Entity_GetInstance), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "EntityInstance AddInstance(const vec3 &in)", asFUNCTION(Entity_AddInstance), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "uint GetMeshCount() const", asFUNCTION(Entity_GetMeshCount), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "Mesh GetMesh(uint) const", asFUNCTION(Entity_GetMesh), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "Material GetMaterial(int) const", asFUNCTION(Entity_GetMaterial), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "string get_name() const property", asFUNCTION(Entity_GetName), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "void set_name(const string &in) property", asFUNCTION(Entity_SetName), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "void Destroy()", asFUNCTION(Entity_Destroy), asCALL_CDECL_OBJLAST);

    engine->RegisterObjectMethod("EntityInstance", "bool IsValid() const", asFUNCTION(Instance_IsValid), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "vec3 get_position() const property", asFUNCTION(Instance_GetPosition), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "void set_position(const vec3 &in) property", asFUNCTION(Instance_SetPosition), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "vec3 get_rotation() const property", asFUNCTION(Instance_GetRotation), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "void set_rotation(const vec3 &in) property", asFUNCTION(Instance_SetRotation), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "vec3 get_scale() const property", asFUNCTION(Instance_GetScale), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "void set_scale(const vec3 &in) property", asFUNCTION(Instance_SetScale), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "mat4 GetTransform() const", asFUNCTION(Instance_GetTransform), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "Entity GetEntity() const", asFUNCTION(Instance_GetEntity), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("EntityInstance", "Material GetMaterial(int) const", asFUNCTION(Instance_GetMaterial), asCALL_CDECL_OBJLAST);

    engine->RegisterObjectMethod("Mesh", "bool IsValid() const", asFUNCTION(Mesh_IsValid), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "vec3 get_position() const property", asFUNCTION(Mesh_GetPosition), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "void set_position(const vec3 &in) property", asFUNCTION(Mesh_SetPosition), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "vec3 get_rotation() const property", asFUNCTION(Mesh_GetRotation), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "void set_rotation(const vec3 &in) property", asFUNCTION(Mesh_SetRotation), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "vec3 get_scale() const property", asFUNCTION(Mesh_GetScale), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "void set_scale(const vec3 &in) property", asFUNCTION(Mesh_SetScale), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "string get_name() const property", asFUNCTION(Mesh_GetName), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "vec3 GetBoundsMin() const", asFUNCTION(Mesh_GetBoundsMin), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "vec3 GetBoundsMax() const", asFUNCTION(Mesh_GetBoundsMax), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "Entity GetEntity() const", asFUNCTION(Mesh_GetEntity), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Mesh", "Material GetMaterial() const", asFUNCTION(Mesh_GetMaterial), asCALL_CDECL_OBJLAST);

    engine->RegisterObjectMethod("Material", "bool IsValid() const", asFUNCTION(Material_IsValid), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void SetBaseColor(const vec4 &in)", asFUNCTION(Material_SetBaseColor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void SetMetallic(float)", asFUNCTION(Material_SetMetallic), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void SetRoughness(float)", asFUNCTION(Material_SetRoughness), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void SetEmissive(const vec3 &in)", asFUNCTION(Material_SetEmissive), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void ClearOverrides()", asFUNCTION(Material_ClearOverrides), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void SetScheme(const string &in)", asFUNCTION(Material_SetScheme), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void SetParam(const string &in, float)", asFUNCTION(Material_SetParamFloat), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void SetParam(const string &in, const vec3 &in)", asFUNCTION(Material_SetParamVec3), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "void SetParam(const string &in, const vec4 &in)", asFUNCTION(Material_SetParamVec4), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Material", "float GetParam(const string &in) const", asFUNCTION(Material_GetParamFloat), asCALL_CDECL_OBJLAST);

    engine->SetDefaultNamespace("Scene");
    engine->RegisterGlobalFunction("Entity SpawnModel(const string &in, const string &in)", asFUNCTION(Scene_SpawnModel), asCALL_CDECL);
    engine->RegisterGlobalFunction("Entity SpawnEmpty(const string &in)", asFUNCTION(Scene_SpawnEmpty), asCALL_CDECL);
    engine->RegisterGlobalFunction("Entity CreateFolder(const string &in)", asFUNCTION(Scene_CreateFolder), asCALL_CDECL);
    engine->RegisterGlobalFunction("Entity FindEntity(const string &in)", asFUNCTION(Scene_FindEntity), asCALL_CDECL);
    engine->RegisterGlobalFunction("void DestroyEntity(const Entity &in)", asFUNCTION(Scene_DestroyEntity), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Reparent(const Entity &in, const Entity &in)", asFUNCTION(Scene_Reparent), asCALL_CDECL);
    engine->SetDefaultNamespace("");
}
