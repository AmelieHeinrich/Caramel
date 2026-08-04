/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Script/ScriptEngine.hpp>
#include <Caramel/Script/ScriptTypes.hpp>

class Scene;
class SceneNode;
class StreamingManager;
class Timer;

struct ScriptInstance
{
    uint64 nodeId = 0;
    uint32 componentIndex = 0;
    asIScriptObject* object = nullptr;
    const ScriptClassInfo* classInfo = nullptr;
    String scriptPath;
    String className;
    uint32 moduleRevision = 0;
    bool startCalled = false;
    bool alive = false;
};

class ScriptSystem
{
public:
    ScriptSystem(Scene& scene, StreamingManager& streamingManager, ScriptEngine& engine);
    ~ScriptSystem();

    void Update(const Timer& timer);

    void RebuildFromScene();

    void ReloadAll();

    // Fires OnStart for a [RunOnce] component on demand -- these do not auto-tick, so that a
    // spawner does not duplicate its content on every hot reload or scene load.
    void RunComponentOnce(SceneNode& node, uint32 componentIndex);

    void OnComponentAdded(SceneNode& node, uint32 componentIndex);
    void OnComponentRemoved(SceneNode& node, uint32 componentIndex);

    bool IsInstanceDrivenByScript(const SceneNode& node, uint32 instanceIndex) const;

    void ApplyProperty(asIScriptObject* object, const ScriptProperty& property, const glm::vec4& value, const String& text);
    void ApplyPropertyToLive(const SceneNode& node, uint32 componentIndex, const ScriptProperty& property);

    ScriptEngine& GetEngine() { return m_Engine; }

private:
    void CollectComponents(SceneNode& node, TArray<ScriptInstance*>& touched);
    ScriptInstance* FindInstance(uint64 nodeId, uint32 componentIndex);
    ScriptInstance* CreateInstance(SceneNode& node, uint32 componentIndex);

    void BindSelf(ScriptInstance& instance, const SceneNode& node, const ScriptComponent& component);
    void RestoreProperties(ScriptInstance& instance, const ScriptComponent& component);
    void SnapshotProperties(ScriptInstance& instance, ScriptComponent& component);

    void DestroyInstance(ScriptInstance& instance);
    void PollHotReload(float32 deltaTime);
    void FlushPendingDestroys();

    void ReportFailure(ScriptInstance& instance, SceneNode& node, const String& stage, const String& error);

    Scene& m_Scene;
    StreamingManager& m_StreamingManager;
    ScriptEngine& m_Engine;

    TArray<TUnique<ScriptInstance>> m_Instances;
    float32 m_WarnCooldown = 0.0f;
};
