/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 16:02:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Scene.hpp"

#include <Caramel/Asset/StreamingManager.hpp>

#include <nlohmann/json.hpp>

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
        j["overrideBaseColor"] = matOverride.overrideBaseColor;
        j["baseColorFactor"] = { matOverride.baseColorFactor.x, matOverride.baseColorFactor.y, matOverride.baseColorFactor.z, matOverride.baseColorFactor.w };
        j["overrideMetallic"] = matOverride.overrideMetallic;
        j["metallicFactor"] = matOverride.metallicFactor;
        j["overrideRoughness"] = matOverride.overrideRoughness;
        j["roughnessFactor"] = matOverride.roughnessFactor;
        j["overrideEmissive"] = matOverride.overrideEmissive;
        j["emissiveFactor"] = { matOverride.emissiveFactor.x, matOverride.emissiveFactor.y, matOverride.emissiveFactor.z };
        return j;
    }

    MaterialOverride ParseMaterialOverride(const nlohmann::json& j)
    {
        MaterialOverride matOverride;
        matOverride.materialIndex = j.value("materialIndex", -1);

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

        return matOverride;
    }

    nlohmann::json SerializeNode(const SceneNode& node)
    {
        nlohmann::json j;
        j["name"] = node.name.CStr();
        j["type"] = node.type == ESceneNodeType::Folder ? "Folder" : "Entity";

        if (node.type == ESceneNodeType::Entity)
        {
            j["cmdl"] = node.cmdlPath.CStr();

            nlohmann::json instancesJson = nlohmann::json::array();
            for (const Instance& instance : node.instances)
                instancesJson.push_back(SerializeInstance(instance));
            j["instances"] = instancesJson;

            nlohmann::json overridesJson = nlohmann::json::array();
            for (const MaterialOverride& matOverride : node.materialOverrides)
                overridesJson.push_back(SerializeMaterialOverride(matOverride));
            j["materialOverrides"] = overridesJson;
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
            for (const auto& overrideJson : j.value("materialOverrides", nlohmann::json::array()))
                node->materialOverrides.PushBack(ParseMaterialOverride(overrideJson));
        }
        else
        {
            node = scene.CreateFolder(parent, name);
        }

        for (const auto& childJson : j.value("children", nlohmann::json::array()))
            ParseNode(childJson, node, scene, streamingManager);
    }
}

SceneNode* Scene::CreateFolder(SceneNode* parent, const String& name)
{
    SceneNode* actualParent = parent ? parent : &m_Root;

    TUnique<SceneNode> node = MakeUnique<SceneNode>();
    node->name = name;
    node->type = ESceneNodeType::Folder;
    node->parent = actualParent;

    SceneNode* raw = node.get();
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
    m_PendingRequests.Insert(raw->requestId, raw);
    actualParent->children.PushBack(std::move(node));
    return raw;
}

void Scene::AddInstance(SceneNode* entity, const Instance& instance)
{
    if (!entity || entity->type != ESceneNodeType::Entity)
        return;

    Instance copy = instance;
    if (copy.name.Empty())
        copy.name = String("Instance " + std::to_string(entity->instances.Size()));

    entity->instances.PushBack(std::move(copy));
}

void Scene::RemovePendingRequests(SceneNode& node)
{
    if (node.type == ESceneNodeType::Entity && node.requestId != 0)
        m_PendingRequests.Erase(node.requestId);

    for (TUnique<SceneNode>& child : node.children)
        RemovePendingRequests(*child);
}

void Scene::DeleteNode(SceneNode* node)
{
    if (!node || node == &m_Root || !node->parent)
        return;

    RemovePendingRequests(*node);

    TArray<TUnique<SceneNode>>& siblings = node->parent->children;
    for (size_t i = 0; i < siblings.Size(); ++i)
    {
        if (siblings[i].get() == node)
        {
            siblings.Erase(i);
            return;
        }
    }
}

void Scene::Reparent(SceneNode* node, SceneNode* newParent)
{
    if (!node || node == &m_Root || !node->parent)
        return;

    SceneNode* actualNewParent = newParent ? newParent : &m_Root;
    if (actualNewParent->type != ESceneNodeType::Folder)
        return;

    if (node->parent == actualNewParent)
        return;

    // Reject cycles: actualNewParent can't be node itself or one of node's own descendants.
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
            // Re-applies (idempotent) on every new mesh discovered for this entity, since the first
            // one to show up is what makes the shared CPUModel's material array reachable at all --
            // needed for entities loaded from a saved scene, whose overrides are already known before
            // any of their meshes have streamed in.
            ApplyMaterialOverrides(*it->second, streamingManager);
        }
    }
}

void Scene::CollectRenderInstances(SceneNode& node, StreamingManager& streamingManager, TArray<RenderInstance>& out)
{
    if (node.type == ESceneNodeType::Entity)
    {
        const TArray<TShared<StreamingModel>>& models = streamingManager.GetModels();
        for (uint32 meshIndex : node.meshIndices)
        {
            StreamingModel* mesh = models[meshIndex].get();
            for (uint32 i = 0; i < (uint32)node.instances.Size(); ++i)
            {
                RenderInstance ri;
                ri.mesh = mesh;
                ri.transform = node.instances[i].GetTransform() * mesh->GetWorldTransform();
                ri.owner = &node;
                ri.instanceIndex = i;
                out.PushBack(ri);
            }
        }
    }

    for (TUnique<SceneNode>& child : node.children)
        CollectRenderInstances(*child, streamingManager, out);
}

TArray<RenderInstance> Scene::BuildRenderInstances(StreamingManager& streamingManager)
{
    TArray<RenderInstance> out;
    CollectRenderInstances(m_Root, streamingManager, out);
    return out;
}

MaterialOverride& Scene::GetOrCreateMaterialOverride(SceneNode& entity, int32 materialIndex)
{
    for (MaterialOverride& matOverride : entity.materialOverrides)
    {
        if (matOverride.materialIndex == materialIndex)
            return matOverride;
    }

    MaterialOverride fresh;
    fresh.materialIndex = materialIndex;
    entity.materialOverrides.PushBack(fresh);
    return entity.materialOverrides[entity.materialOverrides.Size() - 1];
}

void Scene::ApplyMaterialOverrides(SceneNode& entity, StreamingManager& streamingManager)
{
    if (entity.type != ESceneNodeType::Entity || entity.materialOverrides.IsEmpty() || entity.meshIndices.IsEmpty())
        return;

    // Any one of this entity's streamed meshes reaches the same shared CPUModel (one instance per
    // LoadModel() call, shared by every mesh that call produced) -- its full materials array is
    // available as soon as the model itself parsed, which happens before any mesh is queued, so the
    // first mesh to show up in meshIndices is already enough regardless of which materialIndex it is.
    const TArray<TShared<StreamingModel>>& models = streamingManager.GetModels();
    TShared<CPUModel> model = models[entity.meshIndices[0]]->GetSourceModel();
    TArray<ModelMaterial>& materials = model->GetMaterials();

    for (const MaterialOverride& matOverride : entity.materialOverrides)
    {
        if (matOverride.materialIndex < 0 || matOverride.materialIndex >= (int32)materials.Size())
            continue;

        ModelMaterial& material = materials[matOverride.materialIndex];
        if (matOverride.overrideBaseColor)
            material.baseColorFactor = matOverride.baseColorFactor;
        if (matOverride.overrideMetallic)
            material.metallicFactor = matOverride.metallicFactor;
        if (matOverride.overrideRoughness)
            material.roughnessFactor = matOverride.roughnessFactor;
        if (matOverride.overrideEmissive)
            material.emissiveFactor = matOverride.emissiveFactor;
    }
}

bool Scene::SaveToFile(const String& path) const
{
    nlohmann::json j;
    j["version"] = 1;
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

    // StreamingManager has no unload path -- previously-streamed meshes stay GPU-resident but
    // become unreferenced (no longer in any entity's meshIndices, so no longer drawn/pickable).
    m_Root.children.Clear();
    m_PendingRequests.Clear();

    const auto& rootJson = j.at("root");
    for (const auto& childJson : rootJson.value("children", nlohmann::json::array()))
        ParseNode(childJson, &m_Root, *this, streamingManager);

    return true;
}
