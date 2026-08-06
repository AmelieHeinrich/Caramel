/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:02:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Scene.hpp"

#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Core/CpuProfiler.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <vector>

namespace
{
    nlohmann::json SerializeInstance(const Instance& instance)
    {
        nlohmann::json j;
        j["name"] = instance.name.CStr();
        j["position"] = { instance.position.x, instance.position.y, instance.position.z };
        j["rotationEuler"] = { instance.rotationEuler.x, instance.rotationEuler.y, instance.rotationEuler.z };
        j["scale"] = { instance.scale.x, instance.scale.y, instance.scale.z };
        return j;
    }

    Instance ParseInstance(const nlohmann::json& j)
    {
        Instance instance;
        instance.name = j.value("name", "");

        auto p = j.value("position", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
        instance.position = glm::vec3(p[0], p[1], p[2]);

        auto r = j.value("rotationEuler", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
        instance.rotationEuler = glm::vec3(r[0], r[1], r[2]);

        auto s = j.value("scale", std::vector<float32>{ 1.0f, 1.0f, 1.0f });
        instance.scale = glm::vec3(s[0], s[1], s[2]);

        return instance;
    }

    nlohmann::json SerializeMaterialOverride(const MaterialOverride& matOverride)
    {
        nlohmann::json j;
        j["materialIndex"] = matOverride.materialIndex;
        if (matOverride.meshSlot != MaterialOverride::kAllMeshes)
            j["meshSlot"] = matOverride.meshSlot;
        j["overrideBaseColor"] = matOverride.overrideBaseColor;
        j["baseColorFactor"] = { matOverride.baseColorFactor.x, matOverride.baseColorFactor.y, matOverride.baseColorFactor.z, matOverride.baseColorFactor.w };
        j["overrideMetallic"] = matOverride.overrideMetallic;
        j["metallicFactor"] = matOverride.metallicFactor;
        j["overrideRoughness"] = matOverride.overrideRoughness;
        j["roughnessFactor"] = matOverride.roughnessFactor;
        j["overrideEmissive"] = matOverride.overrideEmissive;
        j["emissiveFactor"] = { matOverride.emissiveFactor.x, matOverride.emissiveFactor.y, matOverride.emissiveFactor.z };

        // Scheme params are written as a name -> [x,y,z,w] object rather than packed bytes, so that
        // editing the scheme's parameter list later cannot silently misinterpret a saved scene.
        if (!matOverride.schemeName.Empty())
            j["scheme"] = matOverride.schemeName.CStr();

        if (matOverride.schemeParamValues.Size() > 0)
        {
            nlohmann::json paramsJson = nlohmann::json::object();
            for (const auto& entry : matOverride.schemeParamValues)
                paramsJson[entry.first.CStr()] = { entry.second.x, entry.second.y, entry.second.z, entry.second.w };
            j["schemeParams"] = paramsJson;
        }

        return j;
    }

    MaterialOverride ParseMaterialOverride(const nlohmann::json& j)
    {
        MaterialOverride matOverride;
        matOverride.materialIndex = j.value("materialIndex", -1);
        matOverride.meshSlot = j.value("meshSlot", (int32)MaterialOverride::kAllMeshes);

        matOverride.overrideBaseColor = j.value("overrideBaseColor", false);
        auto bc = j.value("baseColorFactor", std::vector<float32>{ 1.0f, 1.0f, 1.0f, 1.0f });
        matOverride.baseColorFactor = glm::vec4(bc[0], bc[1], bc[2], bc[3]);

        matOverride.overrideMetallic = j.value("overrideMetallic", false);
        matOverride.metallicFactor = j.value("metallicFactor", 1.0f);

        matOverride.overrideRoughness = j.value("overrideRoughness", false);
        matOverride.roughnessFactor = j.value("roughnessFactor", 1.0f);

        matOverride.overrideEmissive = j.value("overrideEmissive", false);
        auto em = j.value("emissiveFactor", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
        matOverride.emissiveFactor = glm::vec3(em[0], em[1], em[2]);

        matOverride.schemeName = j.value("scheme", "");

        if (j.contains("schemeParams") && j["schemeParams"].is_object())
        {
            for (auto it = j["schemeParams"].begin(); it != j["schemeParams"].end(); ++it)
            {
                glm::vec4 value{ 0.0f };
                if (it.value().is_array())
                {
                    const nlohmann::json& arr = it.value();
                    for (uint32 i = 0; i < 4 && i < arr.size(); ++i)
                        value[i] = arr[i].get<float32>();
                }
                else if (it.value().is_number())
                {
                    value.x = it.value().get<float32>();
                }
                matOverride.schemeParamValues[String(it.key())] = value;
            }
        }

        return matOverride;
    }

    nlohmann::json SerializeScriptComponent(const ScriptComponent& component)
    {
        nlohmann::json j;
        j["script"] = component.scriptPath.CStr();
        j["class"] = component.className.CStr();
        j["scope"] = ScriptScopeToString(component.scope);
        j["targetIndex"] = component.targetIndex;
        j["enabled"] = component.enabled;

        // Values are keyed by name rather than packed, so renaming or reordering a script property
        // cannot silently corrupt a saved scene -- the same contract as schemeParamValues above.
        if (component.propertyValues.Size() > 0)
        {
            nlohmann::json props = nlohmann::json::object();
            for (const auto& entry : component.propertyValues)
                props[entry.first.CStr()] = { entry.second.x, entry.second.y, entry.second.z, entry.second.w };
            j["properties"] = props;
        }

        if (component.textPropertyValues.Size() > 0)
        {
            nlohmann::json texts = nlohmann::json::object();
            for (const auto& entry : component.textPropertyValues)
                texts[entry.first.CStr()] = entry.second.CStr();
            j["textProperties"] = texts;
        }

        return j;
    }

    ScriptComponent ParseScriptComponent(const nlohmann::json& j)
    {
        ScriptComponent component;
        component.scriptPath = j.value("script", "");
        component.className = j.value("class", "");
        component.scope = ScriptScopeFromString(String(j.value("scope", "node")));
        component.targetIndex = j.value("targetIndex", 0u);
        component.enabled = j.value("enabled", true);

        if (j.contains("properties") && j["properties"].is_object())
        {
            for (auto it = j["properties"].begin(); it != j["properties"].end(); ++it)
            {
                glm::vec4 value{ 0.0f };
                if (it.value().is_array())
                {
                    const nlohmann::json& arr = it.value();
                    for (uint32 i = 0; i < 4 && i < arr.size(); ++i)
                        value[i] = arr[i].get<float32>();
                }
                else if (it.value().is_number())
                {
                    value.x = it.value().get<float32>();
                }
                else if (it.value().is_boolean())
                {
                    value.x = it.value().get<bool>() ? 1.0f : 0.0f;
                }
                component.propertyValues[String(it.key())] = value;
            }
        }

        if (j.contains("textProperties") && j["textProperties"].is_object())
        {
            for (auto it = j["textProperties"].begin(); it != j["textProperties"].end(); ++it)
                component.textPropertyValues[String(it.key())] = String(it.value().get<std::string>());
        }

        return component;
    }

    const char* NodeTypeToString(ESceneNodeType type)
    {
        switch (type)
        {
            case ESceneNodeType::Entity: return "Entity";
            case ESceneNodeType::Empty:  return "Empty";
            case ESceneNodeType::Light:  return "Light";
            default:                     return "Folder";
        }
    }

    // Light type and shape are written as names rather than enum ordinals, so inserting a type later
    // cannot silently reinterpret every saved light -- the same contract as schemeParams above.
    const char* LightTypeToString(ELightType type)
    {
        switch (type)
        {
            case ELightType::Directional: return "Directional";
            case ELightType::Spot:        return "Spot";
            case ELightType::Area:        return "Area";
            default:                      return "Point";
        }
    }

    ELightType LightTypeFromString(const String& value)
    {
        if (value == "Directional") return ELightType::Directional;
        if (value == "Spot")        return ELightType::Spot;
        if (value == "Area")        return ELightType::Area;
        return ELightType::Point;
    }

    const char* AreaShapeToString(EAreaShape shape)
    {
        switch (shape)
        {
            case EAreaShape::Disk: return "Disk";
            case EAreaShape::Tube: return "Tube";
            default:               return "Rect";
        }
    }

    EAreaShape AreaShapeFromString(const String& value)
    {
        if (value == "Disk") return EAreaShape::Disk;
        if (value == "Tube") return EAreaShape::Tube;
        return EAreaShape::Rect;
    }

    nlohmann::json SerializeLight(const LightComponent& light)
    {
        nlohmann::json j;
        j["type"] = LightTypeToString(light.type);
        j["color"] = { light.color.r, light.color.g, light.color.b };
        j["intensity"] = light.intensity;
        j["range"] = light.range;
        j["innerAngle"] = light.innerAngle;
        j["outerAngle"] = light.outerAngle;
        j["shape"] = AreaShapeToString(light.shape);
        j["size"] = { light.size.x, light.size.y };
        j["twoSided"] = light.twoSided;
        j["sourceRadius"] = light.sourceRadius;
        j["enabled"] = light.enabled;
        return j;
    }

    LightComponent ParseLight(const nlohmann::json& j)
    {
        LightComponent light;
        light.type = LightTypeFromString(String(j.value("type", "Point")));

        auto c = j.value("color", std::vector<float32>{ 1.0f, 1.0f, 1.0f });
        light.color = glm::vec3(c[0], c[1], c[2]);

        light.intensity = j.value("intensity", 1000.0f);
        light.range = j.value("range", 10.0f);
        light.innerAngle = j.value("innerAngle", 20.0f);
        light.outerAngle = j.value("outerAngle", 35.0f);
        light.shape = AreaShapeFromString(String(j.value("shape", "Rect")));

        auto s = j.value("size", std::vector<float32>{ 1.0f, 1.0f });
        light.size = glm::vec2(s[0], s[1]);

        light.twoSided = j.value("twoSided", false);
        light.sourceRadius = j.value("sourceRadius", 0.0f);
        light.enabled = j.value("enabled", true);
        return light;
    }

    nlohmann::json SerializeNode(const SceneNode& node)
    {
        nlohmann::json j;
        j["name"] = node.name.CStr();
        j["type"] = NodeTypeToString(node.type);

        if (node.type == ESceneNodeType::Entity)
            j["cmdl"] = node.cmdlPath.CStr();

        if (SceneNodeTypeHasInstances(node.type))
        {
            nlohmann::json instancesJson = nlohmann::json::array();
            for (const Instance& instance : node.instances)
                instancesJson.push_back(SerializeInstance(instance));
            j["instances"] = instancesJson;
        }

        if (node.type == ESceneNodeType::Entity)
        {
            // Identity offsets are dropped: they are the default, and Sponza-sized entities would
            // otherwise write a few thousand no-op entries.
            nlohmann::json meshTransformsJson = nlohmann::json::array();
            for (const MeshTransform& meshTransform : node.meshTransforms)
            {
                if (meshTransform.IsIdentity())
                    continue;

                nlohmann::json entry;
                entry["meshSlot"] = meshTransform.meshSlot;
                entry["position"] = { meshTransform.position.x, meshTransform.position.y, meshTransform.position.z };
                entry["rotation"] = { meshTransform.rotationEuler.x, meshTransform.rotationEuler.y, meshTransform.rotationEuler.z };
                entry["scale"] = { meshTransform.scale.x, meshTransform.scale.y, meshTransform.scale.z };
                meshTransformsJson.push_back(entry);
            }
            if (!meshTransformsJson.empty())
                j["meshTransforms"] = meshTransformsJson;

            nlohmann::json overridesJson = nlohmann::json::array();
            for (const MaterialOverride& matOverride : node.materialOverrides)
                overridesJson.push_back(SerializeMaterialOverride(matOverride));
            j["materialOverrides"] = overridesJson;
        }

        if (node.type == ESceneNodeType::Light)
            j["light"] = SerializeLight(node.light);

        // Written for every node type -- folders and empties can carry director/spawner scripts.
        if (!node.scripts.IsEmpty())
        {
            nlohmann::json scriptsJson = nlohmann::json::array();
            for (const ScriptComponent& component : node.scripts)
                scriptsJson.push_back(SerializeScriptComponent(component));
            j["scripts"] = scriptsJson;
        }

        nlohmann::json childrenJson = nlohmann::json::array();
        for (const TUnique<SceneNode>& child : node.children)
            childrenJson.push_back(SerializeNode(*child));
        j["children"] = childrenJson;

        return j;
    }

    void ParseNode(const nlohmann::json& j, SceneNode* parent, Scene& scene, StreamingManager& streamingManager)
    {
        String name = j.value("name", "");
        String typeStr = j.value("type", "Folder");

        SceneNode* node;
        if (typeStr == "Entity")
        {
            String cmdlPath = j.value("cmdl", "");
            node = scene.CreateModelEntity(parent, name, cmdlPath, streamingManager);
            for (const auto& instanceJson : j.value("instances", nlohmann::json::array()))
                scene.AddInstance(node, ParseInstance(instanceJson));
            for (const auto& meshTransformJson : j.value("meshTransforms", nlohmann::json::array()))
            {
                MeshTransform meshTransform;
                meshTransform.meshSlot = meshTransformJson.value("meshSlot", 0u);
                auto p = meshTransformJson.value("position", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
                auto r = meshTransformJson.value("rotation", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
                auto s = meshTransformJson.value("scale", std::vector<float32>{ 1.0f, 1.0f, 1.0f });
                meshTransform.position = glm::vec3(p[0], p[1], p[2]);
                meshTransform.rotationEuler = glm::vec3(r[0], r[1], r[2]);
                meshTransform.scale = glm::vec3(s[0], s[1], s[2]);
                node->meshTransforms.PushBack(meshTransform);
            }

            for (const auto& overrideJson : j.value("materialOverrides", nlohmann::json::array()))
                node->materialOverrides.PushBack(ParseMaterialOverride(overrideJson));
        }
        else if (typeStr == "Empty")
        {
            node = scene.CreateEmptyEntity(parent, name);
            node->instances.Clear();
            for (const auto& instanceJson : j.value("instances", nlohmann::json::array()))
                scene.AddInstance(node, ParseInstance(instanceJson));
            if (node->instances.IsEmpty())
                scene.AddInstance(node, Instance());
        }
        else if (typeStr == "Light")
        {
            node = scene.CreateLight(parent, name, ELightType::Point);
            node->instances.Clear();
            for (const auto& instanceJson : j.value("instances", nlohmann::json::array()))
                scene.AddInstance(node, ParseInstance(instanceJson));
            if (node->instances.IsEmpty())
                scene.AddInstance(node, Instance());
            node->light = ParseLight(j.value("light", nlohmann::json::object()));
        }
        else
        {
            node = scene.CreateFolder(parent, name);
        }

        for (const auto& scriptJson : j.value("scripts", nlohmann::json::array()))
            node->scripts.PushBack(ParseScriptComponent(scriptJson));

        for (const auto& childJson : j.value("children", nlohmann::json::array()))
            ParseNode(childJson, node, scene, streamingManager);
    }
}

void Scene::RegisterNode(SceneNode* node)
{
    node->id = m_NextNodeId++;
    m_NodesById.Insert(node->id, node);
}

SceneNode* Scene::FindNodeById(uint64 id)
{
    auto it = m_NodesById.Find(id);
    return it != m_NodesById.End() ? it->second : nullptr;
}

const SceneNode* Scene::FindNodeById(uint64 id) const
{
    auto it = m_NodesById.Find(id);
    return it != m_NodesById.End() ? it->second : nullptr;
}

SceneNode* Scene::FindNodeByName(const String& name)
{
    for (auto& entry : m_NodesById)
    {
        if (entry.second->name == name)
            return entry.second;
    }
    return nullptr;
}

SceneNode* Scene::CreateFolder(SceneNode* parent, const String& name)
{
    SceneNode* actualParent = parent ? parent : &m_Root;

    TUnique<SceneNode> node = MakeUnique<SceneNode>();
    node->name = name;
    node->type = ESceneNodeType::Folder;
    node->parent = actualParent;

    SceneNode* raw = node.get();
    RegisterNode(raw);
    actualParent->children.PushBack(std::move(node));
    return raw;
}

SceneNode* Scene::CreateModelEntity(SceneNode* parent, const String& name, const String& cmdlPath, StreamingManager& streamingManager)
{
    SceneNode* actualParent = parent ? parent : &m_Root;

    TUnique<SceneNode> node = MakeUnique<SceneNode>();
    node->name = name;
    node->type = ESceneNodeType::Entity;
    node->parent = actualParent;
    node->cmdlPath = cmdlPath;
    node->requestId = streamingManager.LoadModel(cmdlPath);

    SceneNode* raw = node.get();
    RegisterNode(raw);
    m_PendingRequests.Insert(raw->requestId, raw);
    actualParent->children.PushBack(std::move(node));
    return raw;
}

SceneNode* Scene::CreateEmptyEntity(SceneNode* parent, const String& name)
{
    SceneNode* actualParent = parent ? parent : &m_Root;

    TUnique<SceneNode> node = MakeUnique<SceneNode>();
    node->name = name;
    node->type = ESceneNodeType::Empty;
    node->parent = actualParent;

    SceneNode* raw = node.get();
    RegisterNode(raw);
    actualParent->children.PushBack(std::move(node));

    AddInstance(raw, Instance());
    return raw;
}

SceneNode* Scene::CreateLight(SceneNode* parent, const String& name, ELightType type)
{
    SceneNode* actualParent = parent ? parent : &m_Root;

    TUnique<SceneNode> node = MakeUnique<SceneNode>();
    node->name = name;
    node->type = ESceneNodeType::Light;
    node->parent = actualParent;
    node->light.type = type;
    node->light.intensity = LightDefaultIntensity(type);

    SceneNode* raw = node.get();
    RegisterNode(raw);
    actualParent->children.PushBack(std::move(node));

    // Above the origin and aimed straight down (-Z rotated by -90 about X), so a freshly created
    // light lands on the scene instead of shooting off sideways from ground level.
    Instance instance;
    instance.position = glm::vec3(0.0f, 3.0f, 0.0f);
    instance.rotationEuler = glm::vec3(-90.0f, 0.0f, 0.0f);
    AddInstance(raw, instance);
    return raw;
}

void Scene::AddInstance(SceneNode* entity, const Instance& instance)
{
    if (!entity || !SceneNodeTypeHasInstances(entity->type))
        return;

    // The one instance a light gets at creation is its transform; a second one would be a light with
    // two positions and no way to author either.
    if (entity->type == ESceneNodeType::Light && !entity->instances.IsEmpty())
        return;

    Instance copy = instance;
    if (copy.name.Empty())
        copy.name = String("Instance " + std::to_string(entity->instances.Size()));

    entity->instances.PushBack(std::move(copy));
    MarkRenderInstancesDirty();
}

void Scene::UnregisterSubtree(SceneNode& node)
{
    if (node.type == ESceneNodeType::Entity && node.requestId != 0)
        m_PendingRequests.Erase(node.requestId);

    if (node.id != 0)
        m_NodesById.Erase(node.id);

    for (TUnique<SceneNode>& child : node.children)
        UnregisterSubtree(*child);
}

void Scene::DeleteNode(SceneNode* node)
{
    if (!node || node == &m_Root || !node->parent)
        return;

    UnregisterSubtree(*node);

    TArray<TUnique<SceneNode>>& siblings = node->parent->children;
    for (size_t i = 0; i < siblings.Size(); ++i)
    {
        if (siblings[i].get() == node)
        {
            siblings.Erase(i);
            MarkRenderInstancesDirty();
            return;
        }
    }
}

void Scene::Reparent(SceneNode* node, SceneNode* newParent)
{
    if (!node || node == &m_Root || !node->parent)
        return;

    SceneNode* actualNewParent = newParent ? newParent : &m_Root;
    if (!SceneNodeTypeCanParent(actualNewParent->type))
        return;

    if (node->parent == actualNewParent)
        return;

    for (SceneNode* p = actualNewParent; p; p = p->parent)
        if (p == node)
            return;

    TArray<TUnique<SceneNode>>& oldSiblings = node->parent->children;
    for (size_t i = 0; i < oldSiblings.Size(); ++i)
    {
        if (oldSiblings[i].get() == node)
        {
            TUnique<SceneNode> moved = std::move(oldSiblings[i]);
            oldSiblings.Erase(i);
            moved->parent = actualNewParent;
            actualNewParent->children.PushBack(std::move(moved));
            // Doesn't change any RenderInstance's content (transforms are world-space, not
            // parent-relative -- see CollectRenderInstances), only traversal order. Marked dirty
            // anyway since nothing downstream is known to depend on that order, and this is rare.
            MarkRenderInstancesDirty();
            return;
        }
    }
}

void Scene::Update(StreamingManager& streamingManager)
{
    const TArray<TShared<StreamingModel>>& models = streamingManager.GetModels();
    for (; m_ScannedModelCount < models.Size(); ++m_ScannedModelCount)
    {
        uint32 requestId = models[m_ScannedModelCount]->GetRequestId();
        auto it = m_PendingRequests.Find(requestId);
        if (it != m_PendingRequests.End())
        {
            it->second->meshIndices.PushBack((uint32)m_ScannedModelCount);
            MarkRenderInstancesDirty();
        }
    }
}

void Scene::CollectRenderInstances(SceneNode& node, StreamingManager& streamingManager, TArray<RenderInstance>& out)
{
    if (node.type == ESceneNodeType::Entity)
    {
        const TArray<TShared<StreamingModel>>& models = streamingManager.GetModels();
        for (uint32 slot = 0; slot < (uint32)node.meshIndices.Size(); ++slot)
        {
            StreamingModel* mesh = models[node.meshIndices[slot]].get();

            // The offset sits between the instance and the model's own placement, so it reads as
            // "move this mesh within the entity" rather than "move it in world space".
            const MeshTransform* meshTransform = FindMeshTransform(node, slot);
            glm::mat4 meshOffset = meshTransform ? meshTransform->GetTransform() : glm::mat4(1.0f);

            for (uint32 i = 0; i < (uint32)node.instances.Size(); ++i)
            {
                RenderInstance ri;
                ri.mesh = mesh;
                ri.transform = node.instances[i].GetTransform() * meshOffset * mesh->GetWorldTransform();
                ri.owner = &node;
                ri.instanceIndex = i;
                ri.meshSlot = slot;
                out.PushBack(ri);
            }
        }
    }

    for (TUnique<SceneNode>& child : node.children)
        CollectRenderInstances(*child, streamingManager, out);
}

const TArray<RenderInstance>& Scene::BuildRenderInstances(StreamingManager& streamingManager)
{
    CARAMEL_ZONE("Scene::BuildRenderInstances");
    if (!m_RenderInstancesDirty)
        return m_CachedRenderInstances;

    m_CachedRenderInstances.Clear();
    CollectRenderInstances(m_Root, streamingManager, m_CachedRenderInstances);
    m_RenderInstancesDirty = false;
    return m_CachedRenderInstances;
}

void Scene::CollectLights(SceneNode& node, TArray<SceneLight>& out)
{
    if (node.type == ESceneNodeType::Light && node.light.enabled && !node.instances.IsEmpty())
    {
        const LightComponent& source = node.light;
        glm::mat4 transform = node.instances[0].GetTransform();

        // Axes are normalized rather than taken with their scale: a light's extent is authored in
        // LightComponent::size, so letting the transform's scale in too would mean two controls
        // fighting over the same number.
        glm::vec3 rightAxis(transform[0]);
        glm::vec3 upAxis(transform[1]);
        glm::vec3 forwardAxis(transform[2]);
        float32 rightLength = glm::length(rightAxis);
        float32 upLength = glm::length(upAxis);
        float32 forwardLength = glm::length(forwardAxis);

        SceneLight light;
        light.type = source.type;
        light.shape = source.shape;
        light.position = glm::vec3(transform[3]);
        light.direction = forwardLength > 0.0f ? -forwardAxis / forwardLength : glm::vec3(0.0f, -1.0f, 0.0f);
        light.right = rightLength > 0.0f ? rightAxis / rightLength : glm::vec3(1.0f, 0.0f, 0.0f);
        light.up = upLength > 0.0f ? upAxis / upLength : glm::vec3(0.0f, 1.0f, 0.0f);
        light.color = source.color;
        light.size = glm::max(source.size, glm::vec2(0.0f));
        light.sourceRadius = source.sourceRadius;
        light.twoSided = source.twoSided;
        light.outerAngle = source.outerAngle;

        // A directional light has no falloff and no bound, which zero range is the sentinel for.
        light.range = source.type == ELightType::Directional ? 0.0f : glm::max(source.range, 0.0f);

        constexpr float32 kPi = 3.14159265f;
        constexpr float32 kFourPi = 4.0f * kPi;

        switch (source.type)
        {
            // Lumens spread over the whole sphere even for a spot. Normalising by the cone's solid
            // angle instead would make dragging the outer angle change how bright the centre of the
            // beam is, which reads as a bug rather than as energy conservation -- and it would make
            // switching a light between Point and Spot discontinuous.
            case ELightType::Point:
            case ELightType::Spot:
                light.intensity = source.intensity / kFourPi;
                break;

            // Nits are luminance; integrating over the emitter's area gives the candela the punctual
            // approximation in the shading loop needs. LTC will replace that approximation and use
            // the size directly, but the unit stays right either way.
            case ELightType::Area:
            {
                float32 area = 0.0f;
                switch (source.shape)
                {
                    case EAreaShape::Rect: area = light.size.x * light.size.y; break;
                    case EAreaShape::Disk: area = kPi * light.size.x * light.size.x; break;
                    case EAreaShape::Tube: area = 2.0f * kPi * light.size.x * light.size.y; break;
                }
                light.intensity = source.intensity * area;
                break;
            }

            // Lux is already an illuminance, which is what a directional light delivers.
            default:
                light.intensity = source.intensity;
                break;
        }

        light.intensity *= kLightExposure;

        if (source.type == ELightType::Spot)
        {
            float32 outer = glm::clamp(source.outerAngle, 0.1f, 89.9f);
            float32 cosOuter = std::cos(glm::radians(outer));
            float32 cosInner = std::cos(glm::radians(glm::clamp(source.innerAngle, 0.0f, outer)));
            light.spotScale = 1.0f / glm::max(cosInner - cosOuter, 1e-4f);
            light.spotOffset = -cosOuter * light.spotScale;
        }
        else
        {
            // saturate(cosAngle * 0 + 1) == 1, so the shader applies the cone term unconditionally
            // instead of branching on the type a second time.
            light.spotScale = 0.0f;
            light.spotOffset = 1.0f;
        }

        light.owner = &node;
        out.PushBack(light);
    }

    for (TUnique<SceneNode>& child : node.children)
        CollectLights(*child, out);
}

const TArray<SceneLight>& Scene::BuildLights()
{
    CARAMEL_ZONE("Scene::BuildLights");
    m_Lights.Clear();
    CollectLights(m_Root, m_Lights);
    return m_Lights;
}

const SceneLight* Scene::FindLight(const SceneNode& node) const
{
    for (const SceneLight& light : m_Lights)
    {
        if (light.owner == &node)
            return &light;
    }
    return nullptr;
}

MeshTransform& Scene::GetOrCreateMeshTransform(SceneNode& entity, uint32 meshSlot)
{
    for (MeshTransform& meshTransform : entity.meshTransforms)
    {
        if (meshTransform.meshSlot == meshSlot)
            return meshTransform;
    }

    MeshTransform fresh;
    fresh.meshSlot = meshSlot;
    entity.meshTransforms.PushBack(fresh);
    // Only reached from edit-intent call sites (inspector, script bindings) that immediately write
    // into the returned reference -- see MarkRenderInstancesDirty's doc comment.
    MarkRenderInstancesDirty();
    return entity.meshTransforms[entity.meshTransforms.Size() - 1];
}

const MeshTransform* Scene::FindMeshTransform(const SceneNode& entity, uint32 meshSlot) const
{
    for (const MeshTransform& meshTransform : entity.meshTransforms)
    {
        if (meshTransform.meshSlot == meshSlot)
            return &meshTransform;
    }
    return nullptr;
}

MaterialOverride& Scene::GetOrCreateMaterialOverride(SceneNode& entity, int32 materialIndex, int32 meshSlot)
{
    for (MaterialOverride& matOverride : entity.materialOverrides)
    {
        if (matOverride.materialIndex == materialIndex && matOverride.meshSlot == meshSlot)
            return matOverride;
    }

    MaterialOverride fresh;

    // A per-mesh override shadows the entity-wide one completely once it exists, so seed it from
    // that value first. Without this, narrowing one field to a single mesh would silently snap every
    // other field on that mesh back to the cooked material.
    if (meshSlot != MaterialOverride::kAllMeshes)
    {
        if (const MaterialOverride* inherited = FindMaterialOverride(entity, materialIndex, MaterialOverride::kAllMeshes))
            fresh = *inherited;
    }

    fresh.materialIndex = materialIndex;
    fresh.meshSlot = meshSlot;
    fresh.gpuMaterialSlot = UINT32_MAX;
    entity.materialOverrides.PushBack(fresh);
    return entity.materialOverrides[entity.materialOverrides.Size() - 1];
}

const MaterialOverride* Scene::FindMaterialOverride(const SceneNode& entity, int32 materialIndex, int32 meshSlot) const
{
    const MaterialOverride* wholeMaterial = nullptr;

    for (const MaterialOverride& matOverride : entity.materialOverrides)
    {
        // An override with nothing set is ignored by GPUScene, so ignore it here too -- otherwise a
        // mesh whose override was fully reverted would display values the renderer no longer uses.
        if (matOverride.materialIndex != materialIndex || !matOverride.HasAnyOverride())
            continue;

        if (meshSlot != MaterialOverride::kAllMeshes && matOverride.meshSlot == meshSlot)
            return &matOverride;

        if (matOverride.meshSlot == MaterialOverride::kAllMeshes)
            wholeMaterial = &matOverride;
    }

    return wholeMaterial;
}

bool Scene::SaveToFile(const String& path) const
{
    nlohmann::json j;
    j["version"] = 3;
    j["root"] = SerializeNode(m_Root);

    std::ofstream file(path.CStr());
    if (!file)
        return false;

    file << j.dump(4);
    return file.good();
}

bool Scene::LoadFromFile(const String& path, StreamingManager& streamingManager)
{
    std::ifstream file(path.CStr());
    if (!file)
        return false;

    nlohmann::json j;
    file >> j;

    m_Root.children.Clear();
    m_PendingRequests.Clear();
    m_NodesById.Clear();
    m_Lights.Clear();
    m_NextNodeId = 1;

    const auto& rootJson = j.at("root");
    for (const auto& childJson : rootJson.value("children", nlohmann::json::array()))
        ParseNode(childJson, &m_Root, *this, streamingManager);

    MarkRenderInstancesDirty();
    return true;
}
