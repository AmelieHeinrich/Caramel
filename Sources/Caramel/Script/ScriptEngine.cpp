/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Script/ScriptEngine.hpp>
#include <Caramel/Script/ScriptBindings.hpp>
#include <Caramel/Core/Logger.hpp>

#include <add_on/scriptbuilder/scriptbuilder.h>
#include <add_on/scriptstdstring/scriptstdstring.h>
#include <add_on/scriptarray/scriptarray.h>
#include <add_on/scriptmath/scriptmath.h>

#include <chrono>
#include <filesystem>
#include <string>

ScriptEngine* ScriptEngine::s_Instance = nullptr;

namespace
{
    struct ExecutionBudget
    {
        std::chrono::steady_clock::time_point start;
        bool aborted = false;
    };

    String TrimWhitespace(const String& value)
    {
        size_t begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            return String();
        size_t end = value.find_last_not_of(" \t\r\n");
        return String(value.substr(begin, end - begin + 1));
    }

    String StripQuotes(const String& value)
    {
        if (value.Size() >= 2 && value.front() == '"' && value.back() == '"')
            return String(value.substr(1, value.Size() - 2));
        return value;
    }

    void ParseMetadataTag(const String& raw, String& outName, TArray<String>& outArgs)
    {
        outArgs.Clear();

        size_t open = raw.find('(');
        if (open == std::string::npos) {
            outName = TrimWhitespace(raw);
            return;
        }

        outName = TrimWhitespace(String(raw.substr(0, open)));

        size_t close = raw.rfind(')');
        if (close == std::string::npos || close <= open)
            return;

        String body = String(raw.substr(open + 1, close - open - 1));

        int32 depth = 0;
        String current;
        for (char c : body) {
            if (c == '(' || c == '[')
                depth++;
            if (c == ')' || c == ']')
                depth--;

            if (c == ',' && depth == 0) {
                outArgs.PushBack(StripQuotes(TrimWhitespace(current)));
                current.Clear();
                continue;
            }
            current.PushBack(c);
        }

        String tail = StripQuotes(TrimWhitespace(current));
        if (!tail.Empty())
            outArgs.PushBack(tail);
    }
}

ScriptEngine& ScriptEngine::Get()
{
    return *s_Instance;
}

ScriptEngine::ScriptEngine()
{
    s_Instance = this;

    m_Engine = asCreateScriptEngine();
    m_Engine->SetMessageCallback(asFUNCTION(MessageCallback), this, asCALL_CDECL);
    m_Engine->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1);

    // Order matters: RegisterStdStringUtils declares array<string> overloads, so the array type has
    // to exist by the time it runs.
    RegisterStdString(m_Engine);
    RegisterScriptArray(m_Engine, true);
    RegisterStdStringUtils(m_Engine);
    RegisterScriptMath(m_Engine);

    RegisterMathTypes(m_Engine);
    RegisterSceneAPI(m_Engine);
    RegisterDebugAPI(m_Engine);

    m_Engine->SetContextCallbacks(RequestContextCallback, ReturnContextCallback, this);
}

ScriptEngine::~ScriptEngine()
{
    m_Engine->SetContextCallbacks(nullptr, nullptr, nullptr);

    for (asIScriptContext* ctx : m_ContextPool)
        ctx->Release();
    m_ContextPool.Clear();

    for (TUnique<ScriptModuleInfo>& info : m_Modules) {
        if (info->module)
            m_Engine->DiscardModule(info->moduleName.CStr());
    }
    m_Modules.Clear();

    m_Engine->ShutDownAndRelease();
    m_Engine = nullptr;

    s_Instance = nullptr;
}

void ScriptEngine::MessageCallback(const asSMessageInfo* msg, void* param)
{
    switch (msg->type) {
        case asMSGTYPE_ERROR:
            CARAMEL_ERROR("[Script] {}:{}:{}: {}", msg->section, msg->row, msg->col, msg->message);
            break;
        case asMSGTYPE_WARNING:
            CARAMEL_WARN("[Script] {}:{}:{}: {}", msg->section, msg->row, msg->col, msg->message);
            break;
        default:
            CARAMEL_INFO("[Script] {}:{}:{}: {}", msg->section, msg->row, msg->col, msg->message);
            break;
    }
}

asIScriptContext* ScriptEngine::RequestContextCallback(asIScriptEngine* engine, void* param)
{
    ScriptEngine* self = (ScriptEngine*)param;
    if (!self->m_ContextPool.IsEmpty()) {
        asIScriptContext* ctx = self->m_ContextPool.back();
        self->m_ContextPool.PopBack();
        return ctx;
    }
    return engine->CreateContext();
}

void ScriptEngine::ReturnContextCallback(asIScriptEngine* engine, asIScriptContext* ctx, void* param)
{
    ScriptEngine* self = (ScriptEngine*)param;
    ctx->Unprepare();

    if (self->m_ContextPool.Size() >= kMaxPooledContexts) {
        ctx->Release();
        return;
    }
    self->m_ContextPool.PushBack(ctx);
}

void ScriptEngine::LineCallback(asIScriptContext* ctx, void* param)
{
    ExecutionBudget* budget = (ExecutionBudget*)param;
    if (budget->aborted)
        return;

    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    float64 elapsed = std::chrono::duration<float64>(now - budget->start).count();
    if (elapsed > kExecutionBudgetSeconds) {
        budget->aborted = true;
        ctx->Abort();
    }
}

void ScriptEngine::SetScriptDirectory(const String& directory)
{
    m_Directory = directory;
    ScanDirectory();
}

uint64 ScriptEngine::ReadFileTime(const String& path)
{
    std::error_code error;
    std::filesystem::file_time_type time = std::filesystem::last_write_time(path.CStr(), error);
    if (error)
        return 0;
    return (uint64)time.time_since_epoch().count();
}

void ScriptEngine::ScanDirectory()
{
    if (m_Directory.Empty())
        return;

    std::error_code error;
    if (!std::filesystem::exists(m_Directory.CStr(), error))
        return;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_Directory.CStr(), error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".as")
            continue;
        LoadModule(String(entry.path().generic_string()));
    }
}

ScriptModuleInfo* ScriptEngine::FindModuleMutable(const String& path)
{
    for (TUnique<ScriptModuleInfo>& info : m_Modules) {
        if (info->path == path)
            return info.get();
    }
    return nullptr;
}

const ScriptModuleInfo* ScriptEngine::FindModule(const String& path) const
{
    for (const TUnique<ScriptModuleInfo>& info : m_Modules) {
        if (info->path == path)
            return info.get();
    }
    return nullptr;
}

const ScriptClassInfo* ScriptEngine::FindClass(const String& path, const String& className) const
{
    const ScriptModuleInfo* info = FindModule(path);
    if (!info || !info->valid)
        return nullptr;

    for (const ScriptClassInfo& classInfo : info->classes) {
        if (classInfo.name == className)
            return &classInfo;
    }
    return nullptr;
}

TArray<String> ScriptEngine::GetKnownScriptPaths() const
{
    TArray<String> paths;
    for (const TUnique<ScriptModuleInfo>& info : m_Modules)
        paths.PushBack(info->path);
    return paths;
}

bool ScriptEngine::LoadModule(const String& path)
{
    if (FindModuleMutable(path))
        return true;

    TUnique<ScriptModuleInfo> info = MakeUnique<ScriptModuleInfo>();
    info->path = path;

    ScriptModuleInfo* raw = info.get();
    m_Modules.PushBack(std::move(info));

    return BuildModule(*raw);
}

bool ScriptEngine::RebuildModule(const String& path)
{
    ScriptModuleInfo* info = FindModuleMutable(path);
    if (!info)
        return LoadModule(path);
    return BuildModule(*info);
}

void ScriptEngine::ReloadAll()
{
    for (TUnique<ScriptModuleInfo>& info : m_Modules)
        BuildModule(*info);
}

bool ScriptEngine::BuildModule(ScriptModuleInfo& info)
{
    uint32 revision = m_NextRevision++;
    String candidateName = info.path + "#" + String(std::to_string(revision));

    info.mtime = ReadFileTime(info.path);

    CScriptBuilder builder;
    if (builder.StartNewModule(m_Engine, candidateName.CStr()) < 0) {
        CARAMEL_ERROR("[Script] Failed to start module for {}", info.path.CStr());
        return false;
    }

    if (builder.AddSectionFromFile(info.path.CStr()) < 0) {
        CARAMEL_ERROR("[Script] Failed to read {}", info.path.CStr());
        m_Engine->DiscardModule(candidateName.CStr());
        return false;
    }

    if (builder.BuildModule() < 0) {
        CARAMEL_ERROR("[Script] Compilation failed for {}", info.path.CStr());
        m_Engine->DiscardModule(candidateName.CStr());
        return false;
    }

    if (info.module)
        m_Engine->DiscardModule(info.moduleName.CStr());

    info.module = builder.GetModule();
    info.moduleName = candidateName;
    info.revision = revision;
    info.classes.Clear();
    info.valid = true;

    ReflectModule(info, builder);

    CARAMEL_INFO("[Script] Compiled {} ({} class(es))", info.path.CStr(), info.classes.Size());
    return true;
}

void ScriptEngine::ReflectModule(ScriptModuleInfo& info, CScriptBuilder& builder)
{
    for (asUINT i = 0; i < info.module->GetObjectTypeCount(); ++i) {
        asITypeInfo* type = info.module->GetObjectTypeByIndex(i);
        if (!type)
            continue;

        ScriptClassInfo classInfo;
        ReflectClass(classInfo, builder, type);
        info.classes.PushBack(std::move(classInfo));
    }
}

void ScriptEngine::ReflectClass(ScriptClassInfo& out, CScriptBuilder& builder, asITypeInfo* type)
{
    out.name = String(type->GetName());
    out.type = type;

    String factoryDecl = out.name + " @" + out.name + "()";
    out.factory = type->GetFactoryByDecl(factoryDecl.CStr());
    out.onStart = type->GetMethodByDecl("void OnStart()");
    out.onUpdate = type->GetMethodByDecl("void OnUpdate(float)");

    for (const std::string& raw : builder.GetMetadataForType(type->GetTypeId())) {
        String name;
        TArray<String> args;
        ParseMetadataTag(String(raw), name, args);

        if (name == "RunOnce") {
            out.runOnce = true;
        } else {
            CARAMEL_WARN("[Script] Unknown class metadata [{}] on {}", name.CStr(), out.name.CStr());
        }
    }

    int32 vec2TypeId = m_Engine->GetTypeIdByDecl("vec2");
    int32 vec3TypeId = m_Engine->GetTypeIdByDecl("vec3");
    int32 vec4TypeId = m_Engine->GetTypeIdByDecl("vec4");
    int32 stringTypeId = m_Engine->GetTypeIdByDecl("string");

    for (asUINT i = 0; i < type->GetPropertyCount(); ++i) {
        const char* propertyName = nullptr;
        int32 typeId = 0;
        bool isPrivate = false;
        bool isProtected = false;
        type->GetProperty(i, &propertyName, &typeId, &isPrivate, &isProtected);

        if (isPrivate || isProtected)
            continue;

        ScriptProperty property;
        property.name = String(propertyName);
        property.propertyIndex = i;
        property.typeId = typeId;

        if (property.name == "self")
            continue;

        bool supported = true;
        if (typeId == asTYPEID_BOOL) {
            property.type = EScriptPropertyType::Bool;
        } else if (typeId == asTYPEID_INT32 || typeId == asTYPEID_UINT32) {
            property.type = EScriptPropertyType::Int;
        } else if (typeId == asTYPEID_FLOAT) {
            property.type = EScriptPropertyType::Float;
        } else if (typeId == vec2TypeId) {
            property.type = EScriptPropertyType::Float2;
        } else if (typeId == vec3TypeId) {
            property.type = EScriptPropertyType::Float3;
        } else if (typeId == vec4TypeId) {
            property.type = EScriptPropertyType::Float4;
        } else if (typeId == stringTypeId) {
            property.type = EScriptPropertyType::Text;
        } else {
            supported = false;
        }

        if (!supported)
            continue;

        for (const std::string& raw : builder.GetMetadataForTypeProperty(type->GetTypeId(), (int)i)) {
            String name;
            TArray<String> args;
            ParseMetadataTag(String(raw), name, args);

            if (name == "Range" && args.Size() >= 2) {
                property.hasRange = true;
                property.minValue = std::strtof(args[0].CStr(), nullptr);
                property.maxValue = std::strtof(args[1].CStr(), nullptr);
            } else if (name == "Color") {
                property.isColor = true;
            } else if (name == "Tooltip" && args.Size() >= 1) {
                property.tooltip = args[0];
            } else if (name == "Hidden") {
                property.hidden = true;
            } else {
                CARAMEL_WARN("[Script] Unknown metadata [{}] on {}::{}", name.CStr(), out.name.CStr(), property.name.CStr());
            }
        }

        out.properties.PushBack(std::move(property));
    }

    CaptureDefaults(out);
}

void ScriptEngine::CaptureDefaults(ScriptClassInfo& classInfo)
{
    if (classInfo.properties.IsEmpty())
        return;

    asIScriptObject* probe = Instantiate(classInfo);
    if (!probe)
        return;

    for (ScriptProperty& property : classInfo.properties) {
        void* address = probe->GetAddressOfProperty(property.propertyIndex);
        if (!address)
            continue;

        switch (property.type) {
            case EScriptPropertyType::Bool:
                property.defaultValue.x = *(bool*)address ? 1.0f : 0.0f;
                break;
            case EScriptPropertyType::Int:
                property.defaultValue.x = (float32)*(int32*)address;
                break;
            case EScriptPropertyType::Float:
                property.defaultValue.x = *(float32*)address;
                break;
            case EScriptPropertyType::Float2:
                property.defaultValue = glm::vec4(*(glm::vec2*)address, 0.0f, 0.0f);
                break;
            case EScriptPropertyType::Float3:
                property.defaultValue = glm::vec4(*(glm::vec3*)address, 0.0f);
                break;
            case EScriptPropertyType::Float4:
                property.defaultValue = *(glm::vec4*)address;
                break;
            case EScriptPropertyType::Text:
                property.defaultText = String(*(std::string*)address);
                break;
        }
    }

    ReleaseObject(probe);
}

asIScriptObject* ScriptEngine::Instantiate(const ScriptClassInfo& classInfo)
{
    if (!classInfo.factory || !classInfo.type)
        return nullptr;

    asIScriptContext* ctx = m_Engine->RequestContext();
    if (!ctx)
        return nullptr;

    asIScriptObject* result = nullptr;
    if (ctx->Prepare(classInfo.factory) >= 0) {
        String error;
        if (Execute(ctx, error) == EScriptCallResult::Ok) {
            result = *(asIScriptObject**)ctx->GetAddressOfReturnValue();
            if (result)
                result->AddRef();
        } else if (!error.Empty()) {
            CARAMEL_ERROR("[Script] Failed to construct {}: {}", classInfo.name.CStr(), error.CStr());
        }
    }

    m_Engine->ReturnContext(ctx);
    return result;
}

void ScriptEngine::ReleaseObject(asIScriptObject* object)
{
    if (object)
        object->Release();
}

EScriptCallResult ScriptEngine::Execute(asIScriptContext* ctx, String& outError)
{
    ExecutionBudget budget;
    budget.start = std::chrono::steady_clock::now();

    ctx->SetLineCallback(asFUNCTION(LineCallback), &budget, asCALL_CDECL);

    int32 result = ctx->Execute();

    ctx->ClearLineCallback();

    if (result == asEXECUTION_FINISHED)
        return EScriptCallResult::Ok;

    if (result == asEXECUTION_ABORTED && budget.aborted) {
        outError = "execution budget exceeded";
        return EScriptCallResult::Aborted;
    }

    if (result == asEXECUTION_EXCEPTION) {
        const asIScriptFunction* function = ctx->GetExceptionFunction();
        outError = String(ctx->GetExceptionString());
        if (function) {
            outError += String(" in ") + String(function->GetDeclaration());
            outError += String(" line ") + String(std::to_string(ctx->GetExceptionLineNumber()));
        }
        return EScriptCallResult::Exception;
    }

    return EScriptCallResult::Ok;
}

EScriptCallResult ScriptEngine::CallVoidMethod(asIScriptObject* object, asIScriptFunction* method, String& outError)
{
    if (!object || !method)
        return EScriptCallResult::NoObject;

    asIScriptContext* ctx = m_Engine->RequestContext();
    if (!ctx)
        return EScriptCallResult::NoObject;

    EScriptCallResult result = EScriptCallResult::NoObject;
    if (ctx->Prepare(method) >= 0) {
        ctx->SetObject(object);
        result = Execute(ctx, outError);
    }

    m_Engine->ReturnContext(ctx);
    return result;
}

EScriptCallResult ScriptEngine::CallUpdate(asIScriptObject* object, asIScriptFunction* method, float32 deltaTime, String& outError)
{
    if (!object || !method)
        return EScriptCallResult::NoObject;

    asIScriptContext* ctx = m_Engine->RequestContext();
    if (!ctx)
        return EScriptCallResult::NoObject;

    EScriptCallResult result = EScriptCallResult::NoObject;
    if (ctx->Prepare(method) >= 0) {
        ctx->SetObject(object);
        ctx->SetArgFloat(0, deltaTime);
        result = Execute(ctx, outError);
    }

    m_Engine->ReturnContext(ctx);
    return result;
}

void ScriptEngine::CollectChangedModules(float32 deltaTime, TArray<String>& outChanged, TArray<String>& outRemoved, TArray<String>& outAdded)
{
    m_PollAccumulator += deltaTime;
    if (m_PollAccumulator < kPollInterval)
        return;
    m_PollAccumulator = 0.0f;

    for (TUnique<ScriptModuleInfo>& info : m_Modules) {
        uint64 mtime = ReadFileTime(info->path);
        if (mtime == 0) {
            outRemoved.PushBack(info->path);
            continue;
        }
        if (mtime != info->mtime)
            outChanged.PushBack(info->path);
    }

    if (m_Directory.Empty())
        return;

    std::error_code error;
    if (!std::filesystem::exists(m_Directory.CStr(), error))
        return;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_Directory.CStr(), error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".as")
            continue;

        String path = String(entry.path().generic_string());
        if (!FindModule(path))
            outAdded.PushBack(path);
    }
}
