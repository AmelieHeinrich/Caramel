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

    uint32 Entity_GetChildCount(const EntityRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(*self);
        return node ? (uint32)node->children.Size() : 0;
    }

    EntityRef Entity_GetChild(uint32 index, const EntityRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(*self);
        if (!node || index >= node->children.Size())
            return EntityRef{};
        return EntityRef{ node->children[index]->id };
    }

    // The light this node carries, or an invalid handle when it is not a light node. Lets a script
    // walk children and pick out lights without a name lookup, which is O(nodes) per call.
    LightRef Entity_AsLight(const EntityRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(*self);
        if (!node || node->type != ESceneNodeType::Light)
            return LightRef{};
        return LightRef{ node->id };
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

    bool Light_IsValid(const LightRef* self)
    {
        return ScriptSceneBridge::Resolve(*self) != nullptr;
    }

    // A light's transform lives on its single Instance, so position and rotation proxy through to it
    // rather than being duplicated on LightComponent.
    Instance* Light_ResolveInstance(const LightRef* self)
    {
        return ScriptSceneBridge::Resolve(InstanceRef{ self->nodeId, 0 });
    }

    glm::vec3 Light_GetPosition(const LightRef* self)
    {
        Instance* instance = Light_ResolveInstance(self);
        return instance ? instance->position : glm::vec3(0.0f);
    }

    void Light_SetPosition(const glm::vec3& value, const LightRef* self)
    {
        if (Instance* instance = Light_ResolveInstance(self))
            instance->position = value;
    }

    glm::vec3 Light_GetRotation(const LightRef* self)
    {
        Instance* instance = Light_ResolveInstance(self);
        return instance ? instance->rotationEuler : glm::vec3(0.0f);
    }

    void Light_SetRotation(const glm::vec3& value, const LightRef* self)
    {
        if (Instance* instance = Light_ResolveInstance(self))
            instance->rotationEuler = value;
    }

    // No MarkRenderInstancesDirty anywhere below: Scene::BuildLights rebuilds unconditionally, which
    // is exactly what lets a script animate a light every frame with no invalidation contract.
    int32 Light_GetType(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? (int32)light->type : 0;
    }

    void Light_SetType(int32 value, const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        if (!light || value < 0 || value > (int32)ELightType::Area)
            return;
        light->type = (ELightType)value;
    }

    glm::vec3 Light_GetColor(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? light->color : glm::vec3(1.0f);
    }

    void Light_SetColor(const glm::vec3& value, const LightRef* self)
    {
        if (LightComponent* light = ScriptSceneBridge::Resolve(*self))
            light->color = value;
    }

    float32 Light_GetIntensity(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? light->intensity : 0.0f;
    }

    void Light_SetIntensity(float32 value, const LightRef* self)
    {
        if (LightComponent* light = ScriptSceneBridge::Resolve(*self))
            light->intensity = value;
    }

    float32 Light_GetRange(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? light->range : 0.0f;
    }

    void Light_SetRange(float32 value, const LightRef* self)
    {
        if (LightComponent* light = ScriptSceneBridge::Resolve(*self))
            light->range = value;
    }

    float32 Light_GetInnerAngle(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? light->innerAngle : 0.0f;
    }

    void Light_SetInnerAngle(float32 value, const LightRef* self)
    {
        if (LightComponent* light = ScriptSceneBridge::Resolve(*self))
            light->innerAngle = value;
    }

    float32 Light_GetOuterAngle(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? light->outerAngle : 0.0f;
    }

    void Light_SetOuterAngle(float32 value, const LightRef* self)
    {
        if (LightComponent* light = ScriptSceneBridge::Resolve(*self))
            light->outerAngle = value;
    }

    bool Light_GetEnabled(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? light->enabled : false;
    }

    void Light_SetEnabled(bool value, const LightRef* self)
    {
        if (LightComponent* light = ScriptSceneBridge::Resolve(*self))
            light->enabled = value;
    }

    int32 Light_GetShape(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? (int32)light->shape : 0;
    }

    void Light_SetShape(int32 value, const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        if (!light || value < 0 || value > (int32)EAreaShape::Tube)
            return;
        light->shape = (EAreaShape)value;
    }

    glm::vec2 Light_GetSize(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? light->size : glm::vec2(1.0f);
    }

    void Light_SetSize(const glm::vec2& value, const LightRef* self)
    {
        if (LightComponent* light = ScriptSceneBridge::Resolve(*self))
            light->size = value;
    }

    float32 Light_GetSourceRadius(const LightRef* self)
    {
        LightComponent* light = ScriptSceneBridge::Resolve(*self);
        return light ? light->sourceRadius : 0.0f;
    }

    void Light_SetSourceRadius(float32 value, const LightRef* self)
    {
        if (LightComponent* light = ScriptSceneBridge::Resolve(*self))
            light->sourceRadius = value;
    }

    std::string Light_GetName(const LightRef* self)
    {
        SceneNode* node = ScriptSceneBridge::Resolve(EntityRef{ self->nodeId });
        return node ? std::string(node->name.CStr()) : std::string();
    }

    EntityRef Light_GetEntity(const LightRef* self)
    {
        return EntityRef{ self->nodeId };
    }

    void Light_Destroy(const LightRef* self)
    {
        ScriptSceneBridge::QueueDestroy(self->nodeId);
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

    LightRef Scene_SpawnLight(int32 type, const std::string& name)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        if (!scene || type < 0 || type > (int32)ELightType::Area)
            return LightRef{};

        SceneNode* node = scene->CreateLight(nullptr, String(name), (ELightType)type);
        return node ? LightRef{ node->id } : LightRef{};
    }

    LightRef Scene_FindLight(const std::string& name)
    {
        Scene* scene = ScriptSceneBridge::GetScene();
        if (!scene)
            return LightRef{};

        SceneNode* node = scene->FindNodeByName(String(name));
        if (!node || node->type != ESceneNodeType::Light)
            return LightRef{};
        return LightRef{ node->id };
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
    engine->RegisterObjectType("Light", sizeof(LightRef), handleFlags | asGetTypeTraits<LightRef>());

    // Mirrors ELightType / EAreaShape (Sources/Caramel/Scene/SceneNode.hpp).
    engine->RegisterEnum("LightType");
    engine->RegisterEnumValue("LightType", "Directional", (int)ELightType::Directional);
    engine->RegisterEnumValue("LightType", "Point", (int)ELightType::Point);
    engine->RegisterEnumValue("LightType", "Spot", (int)ELightType::Spot);
    engine->RegisterEnumValue("LightType", "Area", (int)ELightType::Area);

    engine->RegisterEnum("AreaShape");
    engine->RegisterEnumValue("AreaShape", "Rect", (int)EAreaShape::Rect);
    engine->RegisterEnumValue("AreaShape", "Disk", (int)EAreaShape::Disk);
    engine->RegisterEnumValue("AreaShape", "Tube", (int)EAreaShape::Tube);

    engine->RegisterObjectMethod("Entity", "bool IsValid() const", asFUNCTION(Entity_IsValid), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "uint GetInstanceCount() const", asFUNCTION(Entity_GetInstanceCount), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "EntityInstance GetInstance(uint) const", asFUNCTION(Entity_GetInstance), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "EntityInstance AddInstance(const vec3 &in)", asFUNCTION(Entity_AddInstance), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "uint GetMeshCount() const", asFUNCTION(Entity_GetMeshCount), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "Mesh GetMesh(uint) const", asFUNCTION(Entity_GetMesh), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "Material GetMaterial(int) const", asFUNCTION(Entity_GetMaterial), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "uint GetChildCount() const", asFUNCTION(Entity_GetChildCount), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "Entity GetChild(uint) const", asFUNCTION(Entity_GetChild), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Entity", "Light AsLight() const", asFUNCTION(Entity_AsLight), asCALL_CDECL_OBJLAST);
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

    engine->RegisterObjectMethod("Light", "bool IsValid() const", asFUNCTION(Light_IsValid), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "vec3 get_position() const property", asFUNCTION(Light_GetPosition), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_position(const vec3 &in) property", asFUNCTION(Light_SetPosition), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "vec3 get_rotation() const property", asFUNCTION(Light_GetRotation), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_rotation(const vec3 &in) property", asFUNCTION(Light_SetRotation), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "int get_type() const property", asFUNCTION(Light_GetType), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_type(int) property", asFUNCTION(Light_SetType), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "vec3 get_color() const property", asFUNCTION(Light_GetColor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_color(const vec3 &in) property", asFUNCTION(Light_SetColor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "float get_intensity() const property", asFUNCTION(Light_GetIntensity), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_intensity(float) property", asFUNCTION(Light_SetIntensity), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "float get_range() const property", asFUNCTION(Light_GetRange), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_range(float) property", asFUNCTION(Light_SetRange), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "float get_innerAngle() const property", asFUNCTION(Light_GetInnerAngle), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_innerAngle(float) property", asFUNCTION(Light_SetInnerAngle), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "float get_outerAngle() const property", asFUNCTION(Light_GetOuterAngle), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_outerAngle(float) property", asFUNCTION(Light_SetOuterAngle), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "bool get_enabled() const property", asFUNCTION(Light_GetEnabled), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_enabled(bool) property", asFUNCTION(Light_SetEnabled), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "int get_shape() const property", asFUNCTION(Light_GetShape), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_shape(int) property", asFUNCTION(Light_SetShape), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "vec2 get_size() const property", asFUNCTION(Light_GetSize), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_size(const vec2 &in) property", asFUNCTION(Light_SetSize), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "float get_sourceRadius() const property", asFUNCTION(Light_GetSourceRadius), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void set_sourceRadius(float) property", asFUNCTION(Light_SetSourceRadius), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "string get_name() const property", asFUNCTION(Light_GetName), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "Entity GetEntity() const", asFUNCTION(Light_GetEntity), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("Light", "void Destroy()", asFUNCTION(Light_Destroy), asCALL_CDECL_OBJLAST);

    engine->SetDefaultNamespace("Scene");
    engine->RegisterGlobalFunction("Entity SpawnModel(const string &in, const string &in)", asFUNCTION(Scene_SpawnModel), asCALL_CDECL);
    engine->RegisterGlobalFunction("Entity SpawnEmpty(const string &in)", asFUNCTION(Scene_SpawnEmpty), asCALL_CDECL);
    engine->RegisterGlobalFunction("Entity CreateFolder(const string &in)", asFUNCTION(Scene_CreateFolder), asCALL_CDECL);
    engine->RegisterGlobalFunction("Light SpawnLight(int, const string &in)", asFUNCTION(Scene_SpawnLight), asCALL_CDECL);
    engine->RegisterGlobalFunction("Light FindLight(const string &in)", asFUNCTION(Scene_FindLight), asCALL_CDECL);
    engine->RegisterGlobalFunction("Entity FindEntity(const string &in)", asFUNCTION(Scene_FindEntity), asCALL_CDECL);
    engine->RegisterGlobalFunction("void DestroyEntity(const Entity &in)", asFUNCTION(Scene_DestroyEntity), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Reparent(const Entity &in, const Entity &in)", asFUNCTION(Scene_Reparent), asCALL_CDECL);
    engine->SetDefaultNamespace("");
}
