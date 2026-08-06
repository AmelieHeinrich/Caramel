/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Script/ScriptTypes.hpp>

#include <angelscript.h>

class CScriptBuilder;

struct ScriptClassInfo
{
    String name;
    asITypeInfo* type = nullptr;
    asIScriptFunction* factory = nullptr;
    asIScriptFunction* onStart = nullptr;
    asIScriptFunction* onUpdate = nullptr;
    bool runOnce = false;
    TArray<ScriptProperty> properties;
};

struct ScriptModuleInfo
{
    String path;
    String moduleName;
    asIScriptModule* module = nullptr;
    TArray<ScriptClassInfo> classes;
    uint64 mtime = 0;
    uint32 revision = 0;
    bool valid = false;
};

enum class EScriptCallResult
{
    Ok,
    NoObject,
    Exception,
    Aborted
};

class ScriptEngine
{
public:
    static ScriptEngine& Get();

    ScriptEngine();
    ~ScriptEngine();

    void SetScriptDirectory(const String& directory);
    const String& GetScriptDirectory() const { return m_Directory; }

    void ScanDirectory();
    bool LoadModule(const String& path);
    void ReloadAll();

    const ScriptModuleInfo* FindModule(const String& path) const;
    const ScriptClassInfo* FindClass(const String& path, const String& className) const;
    TArray<String> GetKnownScriptPaths() const;

    asIScriptObject* Instantiate(const ScriptClassInfo& classInfo);
    void ReleaseObject(asIScriptObject* object);

    EScriptCallResult CallVoidMethod(asIScriptObject* object, asIScriptFunction* method, String& outError);
    EScriptCallResult CallUpdate(asIScriptObject* object, asIScriptFunction* method, float32 deltaTime, String& outError);

    asIScriptEngine* GetHandle() { return m_Engine; }

    void CollectChangedModules(float32 deltaTime, TArray<String>& outChanged, TArray<String>& outRemoved, TArray<String>& outAdded);
    bool RebuildModule(const String& path);

    static constexpr float32 kPollInterval = 0.25f;
    // OnUpdate runs every single frame, so it keeps a tight leash to avoid hanging the render loop.
    static constexpr float64 kExecutionBudgetSeconds = 1;
    // OnStart runs once (including a manual Run on a [RunOnce] component), so it can afford to
    // burn real time on one-shot setup work like spawning thousands of instances.
    static constexpr float64 kOnStartExecutionBudgetSeconds = 5.0;
    static constexpr uint32 kMaxPooledContexts = 8;

private:
    static void MessageCallback(const asSMessageInfo* msg, void* param);
    static asIScriptContext* RequestContextCallback(asIScriptEngine* engine, void* param);
    static void ReturnContextCallback(asIScriptEngine* engine, asIScriptContext* ctx, void* param);
    static void LineCallback(asIScriptContext* ctx, void* param);

    ScriptModuleInfo* FindModuleMutable(const String& path);
    bool BuildModule(ScriptModuleInfo& info);
    void ReflectModule(ScriptModuleInfo& info, CScriptBuilder& builder);
    void ReflectClass(ScriptClassInfo& out, CScriptBuilder& builder, asITypeInfo* type);
    void CaptureDefaults(ScriptClassInfo& classInfo);
    EScriptCallResult Execute(asIScriptContext* ctx, String& outError, float64 budgetSeconds);

    static uint64 ReadFileTime(const String& path);

    static ScriptEngine* s_Instance;

    asIScriptEngine* m_Engine = nullptr;
    String m_Directory;
    TArray<TUnique<ScriptModuleInfo>> m_Modules;
    TArray<asIScriptContext*> m_ContextPool;
    uint32 m_NextRevision = 1;
    float32 m_PollAccumulator = 0.0f;
};
