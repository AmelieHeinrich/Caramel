/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Script/ScriptSystem.hpp>
#include <Caramel/Script/ScriptHandles.hpp>
#include <Caramel/Script/ScriptTime.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Core/Timer.hpp>
#include <Caramel/Core/Logger.hpp>

#include <string>

ScriptSystem::ScriptSystem(Scene& scene, StreamingManager& streamingManager, ScriptEngine& engine)
    : m_Scene(scene), m_StreamingManager(streamingManager), m_Engine(engine)
{
    ScriptSceneBridge::SetContext(&scene, &streamingManager);
}

ScriptSystem::~ScriptSystem()
{
    for (TUnique<ScriptInstance>& instance : m_Instances)
        DestroyInstance(*instance);
    m_Instances.Clear();

    ScriptSceneBridge::SetContext(nullptr, nullptr);
}

void ScriptSystem::DestroyInstance(ScriptInstance& instance)
{
    if (instance.object) {
        m_Engine.ReleaseObject(instance.object);
        instance.object = nullptr;
    }
    instance.classInfo = nullptr;
}

ScriptInstance* ScriptSystem::FindInstance(uint64 nodeId, uint32 componentIndex)
{
    for (TUnique<ScriptInstance>& instance : m_Instances) {
        if (instance->nodeId == nodeId && instance->componentIndex == componentIndex)
            return instance.get();
    }
    return nullptr;
}

ScriptInstance* ScriptSystem::CreateInstance(SceneNode& node, uint32 componentIndex)
{
    const ScriptComponent& component = node.scripts[componentIndex];

    TUnique<ScriptInstance> instance = MakeUnique<ScriptInstance>();
    instance->nodeId = node.id;
    instance->componentIndex = componentIndex;
    instance->scriptPath = component.scriptPath;
    instance->className = component.className;

    ScriptInstance* raw = instance.get();
    m_Instances.PushBack(std::move(instance));

    const ScriptClassInfo* classInfo = m_Engine.FindClass(component.scriptPath, component.className);
    if (!classInfo)
        return raw;

    raw->classInfo = classInfo;
    raw->object = m_Engine.Instantiate(*classInfo);
    if (!raw->object)
        return raw;

    const ScriptModuleInfo* moduleInfo = m_Engine.FindModule(component.scriptPath);
    raw->moduleRevision = moduleInfo ? moduleInfo->revision : 0;

    BindSelf(*raw, node, component);
    RestoreProperties(*raw, component);

    return raw;
}

void ScriptSystem::BindSelf(ScriptInstance& instance, const SceneNode& node, const ScriptComponent& component)
{
    if (!instance.object || !instance.classInfo || !instance.classInfo->type)
        return;

    asITypeInfo* type = instance.classInfo->type;
    asIScriptEngine* engine = m_Engine.GetHandle();

    int32 entityTypeId = engine->GetTypeIdByDecl("Entity");
    int32 instanceTypeId = engine->GetTypeIdByDecl("EntityInstance");
    int32 meshTypeId = engine->GetTypeIdByDecl("Mesh");

    for (asUINT i = 0; i < type->GetPropertyCount(); ++i) {
        const char* name = nullptr;
        int32 typeId = 0;
        type->GetProperty(i, &name, &typeId);

        if (!name || String(name) != "self")
            continue;

        void* address = instance.object->GetAddressOfProperty(i);
        if (!address)
            return;

        if (typeId == entityTypeId) {
            *(EntityRef*)address = EntityRef{ node.id };
        } else if (typeId == instanceTypeId) {
            uint32 index = component.scope == EScriptScope::Instance ? component.targetIndex : 0;
            *(InstanceRef*)address = InstanceRef{ node.id, index };
        } else if (typeId == meshTypeId) {
            uint32 slot = component.scope == EScriptScope::Mesh ? component.targetIndex : 0;
            *(MeshRef*)address = MeshRef{ node.id, slot };
        } else {
            CARAMEL_WARN("[Script] {}::self must be Entity, EntityInstance or Mesh", instance.className.CStr());
        }
        return;
    }
}

void ScriptSystem::ApplyProperty(asIScriptObject* object, const ScriptProperty& property, const glm::vec4& value, const String& text)
{
    if (!object)
        return;

    // Guard against a ScriptProperty left over from a half-applied reload type-punning the object.
    asITypeInfo* type = object->GetObjectType();
    if (!type || property.propertyIndex >= type->GetPropertyCount())
        return;

    const char* name = nullptr;
    int32 liveTypeId = 0;
    type->GetProperty(property.propertyIndex, &name, &liveTypeId);
    if (liveTypeId != property.typeId)
        return;

    void* address = object->GetAddressOfProperty(property.propertyIndex);
    if (!address)
        return;

    switch (property.type) {
        case EScriptPropertyType::Bool:
            *(bool*)address = value.x != 0.0f;
            break;
        case EScriptPropertyType::Int:
            *(int32*)address = (int32)value.x;
            break;
        case EScriptPropertyType::Float:
            *(float32*)address = value.x;
            break;
        case EScriptPropertyType::Float2:
            *(glm::vec2*)address = glm::vec2(value);
            break;
        case EScriptPropertyType::Float3:
            *(glm::vec3*)address = glm::vec3(value);
            break;
        case EScriptPropertyType::Float4:
            *(glm::vec4*)address = value;
            break;
        case EScriptPropertyType::Text:
            *(std::string*)address = std::string(text.CStr());
            break;
    }
}

void ScriptSystem::RestoreProperties(ScriptInstance& instance, const ScriptComponent& component)
{
    if (!instance.object || !instance.classInfo)
        return;

    for (const ScriptProperty& property : instance.classInfo->properties) {
        if (property.type == EScriptPropertyType::Text) {
            auto it = component.textPropertyValues.Find(property.name);
            if (it != component.textPropertyValues.End())
                ApplyProperty(instance.object, property, glm::vec4(0.0f), it->second);
            continue;
        }

        auto it = component.propertyValues.Find(property.name);
        if (it != component.propertyValues.End())
            ApplyProperty(instance.object, property, it->second, String());
    }
}

void ScriptSystem::SnapshotProperties(ScriptInstance& instance, ScriptComponent& component)
{
    if (!instance.object || !instance.classInfo)
        return;

    asITypeInfo* type = instance.object->GetObjectType();
    if (!type)
        return;

    for (const ScriptProperty& property : instance.classInfo->properties) {
        if (property.propertyIndex >= type->GetPropertyCount())
            continue;

        void* address = instance.object->GetAddressOfProperty(property.propertyIndex);
        if (!address)
            continue;

        glm::vec4 value{ 0.0f };
        switch (property.type) {
            case EScriptPropertyType::Bool:
                value.x = *(bool*)address ? 1.0f : 0.0f;
                break;
            case EScriptPropertyType::Int:
                value.x = (float32)*(int32*)address;
                break;
            case EScriptPropertyType::Float:
                value.x = *(float32*)address;
                break;
            case EScriptPropertyType::Float2:
                value = glm::vec4(*(glm::vec2*)address, 0.0f, 0.0f);
                break;
            case EScriptPropertyType::Float3:
                value = glm::vec4(*(glm::vec3*)address, 0.0f);
                break;
            case EScriptPropertyType::Float4:
                value = *(glm::vec4*)address;
                break;
            case EScriptPropertyType::Text:
                component.textPropertyValues[property.name] = String(*(std::string*)address);
                continue;
        }

        component.propertyValues[property.name] = value;
    }
}

void ScriptSystem::ApplyPropertyToLive(const SceneNode& node, uint32 componentIndex, const ScriptProperty& property)
{
    ScriptInstance* instance = FindInstance(node.id, componentIndex);
    if (!instance || !instance->object)
        return;

    const ScriptComponent& component = node.scripts[componentIndex];

    if (property.type == EScriptPropertyType::Text) {
        auto it = component.textPropertyValues.Find(property.name);
        String text = it != component.textPropertyValues.End() ? it->second : property.defaultText;
        ApplyProperty(instance->object, property, glm::vec4(0.0f), text);
        return;
    }

    auto it = component.propertyValues.Find(property.name);
    glm::vec4 value = it != component.propertyValues.End() ? it->second : property.defaultValue;
    ApplyProperty(instance->object, property, value, String());
}

void ScriptSystem::OnComponentAdded(SceneNode& node, uint32 componentIndex)
{
    if (componentIndex < node.scripts.Size() && !FindInstance(node.id, componentIndex))
        CreateInstance(node, componentIndex);
}

void ScriptSystem::OnComponentRemoved(SceneNode& node, uint32 componentIndex)
{
    // Component indices shift on removal, so drop every instance for this node and let the next
    // Update rebuild them from the surviving components.
    for (size_t i = m_Instances.Size(); i > 0; --i) {
        TUnique<ScriptInstance>& instance = m_Instances[i - 1];
        if (instance->nodeId != node.id)
            continue;

        DestroyInstance(*instance);
        m_Instances.Erase(i - 1);
    }
}

bool ScriptSystem::IsInstanceDrivenByScript(const SceneNode& node, uint32 instanceIndex) const
{
    for (const ScriptComponent& component : node.scripts) {
        if (!component.enabled)
            continue;
        if (component.scope == EScriptScope::Instance && component.targetIndex == instanceIndex)
            return true;
    }
    return false;
}

bool ScriptSystem::IsMeshDrivenByScript(const SceneNode& node, uint32 meshSlot) const
{
    for (const ScriptComponent& component : node.scripts) {
        if (!component.enabled)
            continue;
        if (component.scope == EScriptScope::Mesh && component.targetIndex == meshSlot)
            return true;
    }
    return false;
}

void ScriptSystem::CollectComponents(SceneNode& node, TArray<ScriptInstance*>& touched)
{
    for (uint32 i = 0; i < (uint32)node.scripts.Size(); ++i) {
        ScriptInstance* instance = FindInstance(node.id, i);

        if (instance && (instance->scriptPath != node.scripts[i].scriptPath || instance->className != node.scripts[i].className)) {
            DestroyInstance(*instance);
            instance->scriptPath = node.scripts[i].scriptPath;
            instance->className = node.scripts[i].className;
            instance->startCalled = false;

            const ScriptClassInfo* classInfo = m_Engine.FindClass(instance->scriptPath, instance->className);
            if (classInfo) {
                instance->classInfo = classInfo;
                instance->object = m_Engine.Instantiate(*classInfo);
                BindSelf(*instance, node, node.scripts[i]);
                RestoreProperties(*instance, node.scripts[i]);
            }
        }

        if (!instance)
            instance = CreateInstance(node, i);

        instance->alive = true;
        touched.PushBack(instance);
    }

    for (TUnique<SceneNode>& child : node.children)
        CollectComponents(*child, touched);
}

void ScriptSystem::RebuildFromScene()
{
    for (TUnique<ScriptInstance>& instance : m_Instances)
        DestroyInstance(*instance);
    m_Instances.Clear();

    TArray<ScriptInstance*> touched;
    CollectComponents(m_Scene.GetRoot(), touched);
}

void ScriptSystem::ReportFailure(ScriptInstance& instance, SceneNode& node, const String& stage, const String& error)
{
    CARAMEL_ERROR("[Script] {}::{} {} failed: {}", instance.className.CStr(), stage.CStr(), node.name.CStr(), error.CStr());

    if (instance.componentIndex < node.scripts.Size())
        node.scripts[instance.componentIndex].enabled = false;
}

void ScriptSystem::RunComponentOnce(SceneNode& node, uint32 componentIndex)
{
    ScriptInstance* instance = FindInstance(node.id, componentIndex);
    if (!instance)
        instance = CreateInstance(node, componentIndex);

    if (!instance || !instance->object || !instance->classInfo || !instance->classInfo->onStart)
        return;

    String error;
    EScriptCallResult result = m_Engine.CallVoidMethod(instance->object, instance->classInfo->onStart, error);
    if (result == EScriptCallResult::Exception || result == EScriptCallResult::Aborted)
        ReportFailure(*instance, node, "OnStart", error);
    else
        instance->startCalled = true;
}

void ScriptSystem::PollHotReload(float32 deltaTime)
{
    TArray<String> changed;
    TArray<String> removed;
    TArray<String> added;
    m_Engine.CollectChangedModules(deltaTime, changed, removed, added);

    if (changed.IsEmpty() && added.IsEmpty())
        return;

    // Snapshot live values into the by-name store first, so a rename or a new property in the
    // edited file degrades gracefully instead of losing the user's tuning.
    for (const String& path : changed) {
        for (TUnique<ScriptInstance>& instance : m_Instances) {
            if (instance->scriptPath != path || !instance->object)
                continue;

            SceneNode* node = m_Scene.FindNodeById(instance->nodeId);
            if (node && instance->componentIndex < node->scripts.Size())
                SnapshotProperties(*instance, node->scripts[instance->componentIndex]);
        }
    }

    for (const String& path : changed) {
        if (!m_Engine.RebuildModule(path))
            continue;

        for (TUnique<ScriptInstance>& instance : m_Instances) {
            if (instance->scriptPath != path)
                continue;

            DestroyInstance(*instance);

            SceneNode* node = m_Scene.FindNodeById(instance->nodeId);
            if (!node || instance->componentIndex >= node->scripts.Size())
                continue;

            const ScriptComponent& component = node->scripts[instance->componentIndex];
            const ScriptClassInfo* classInfo = m_Engine.FindClass(path, instance->className);
            if (!classInfo) {
                CARAMEL_WARN("[Script] Class {} no longer exists in {}", instance->className.CStr(), path.CStr());
                continue;
            }

            instance->classInfo = classInfo;
            instance->object = m_Engine.Instantiate(*classInfo);
            instance->startCalled = false;

            const ScriptModuleInfo* moduleInfo = m_Engine.FindModule(path);
            instance->moduleRevision = moduleInfo ? moduleInfo->revision : 0;

            BindSelf(*instance, *node, component);
            RestoreProperties(*instance, component);
        }
    }

    for (const String& path : added)
        m_Engine.LoadModule(path);
}

void ScriptSystem::FlushPendingDestroys()
{
    TArray<uint64>& pending = ScriptSceneBridge::GetPendingDestroys();
    if (pending.IsEmpty())
        return;

    for (uint64 nodeId : pending) {
        SceneNode* node = m_Scene.FindNodeById(nodeId);
        if (node)
            m_Scene.DeleteNode(node);
    }

    ScriptSceneBridge::ClearPendingDestroys();
}

void ScriptSystem::ReloadAll()
{
    for (TUnique<ScriptInstance>& instance : m_Instances) {
        SceneNode* node = m_Scene.FindNodeById(instance->nodeId);
        if (node && instance->componentIndex < node->scripts.Size())
            SnapshotProperties(*instance, node->scripts[instance->componentIndex]);
    }

    m_Engine.ReloadAll();
    RebuildFromScene();
}

void ScriptSystem::Update(const Timer& timer)
{
    float32 deltaTime = timer.GetDelta();
    ScriptTime::Set(deltaTime, timer.GetElapsed(), timer.GetFrameCount());

    PollHotReload(deltaTime);

    for (TUnique<ScriptInstance>& instance : m_Instances)
        instance->alive = false;

    TArray<ScriptInstance*> touched;
    CollectComponents(m_Scene.GetRoot(), touched);

    for (size_t i = m_Instances.Size(); i > 0; --i) {
        if (m_Instances[i - 1]->alive)
            continue;
        DestroyInstance(*m_Instances[i - 1]);
        m_Instances.Erase(i - 1);
    }

    for (TUnique<ScriptInstance>& instance : m_Instances) {
        if (!instance->object || !instance->classInfo)
            continue;

        SceneNode* node = m_Scene.FindNodeById(instance->nodeId);
        if (!node || instance->componentIndex >= node->scripts.Size())
            continue;

        const ScriptComponent& component = node->scripts[instance->componentIndex];
        if (!component.enabled)
            continue;

        if (!instance->startCalled && instance->classInfo->onStart && !instance->classInfo->runOnce) {
            String error;
            EScriptCallResult result = m_Engine.CallVoidMethod(instance->object, instance->classInfo->onStart, error);
            instance->startCalled = true;
            if (result == EScriptCallResult::Exception || result == EScriptCallResult::Aborted) {
                ReportFailure(*instance, *node, "OnStart", error);
                continue;
            }
        }

        if (instance->classInfo->onUpdate) {
            String error;
            EScriptCallResult result = m_Engine.CallUpdate(instance->object, instance->classInfo->onUpdate, deltaTime, error);
            if (result == EScriptCallResult::Exception || result == EScriptCallResult::Aborted)
                ReportFailure(*instance, *node, "OnUpdate", error);
        }
    }

    FlushPendingDestroys();

    m_WarnCooldown -= deltaTime;
    uint32 failedResolves = ScriptSceneBridge::ConsumeFailedResolves();
    if (failedResolves > 0 && m_WarnCooldown <= 0.0f) {
        CARAMEL_WARN("[Script] {} script handle(s) referenced a deleted node or instance", failedResolves);
        m_WarnCooldown = 1.0f;
    }
}
