/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:10:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "MaterialScheme.hpp"

#include <Caramel/Core/Logger.hpp>
#include <Caramel/Renderer/Shader/ShaderServer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

uint32 SchemeParamSize(ESchemeParamType type)
{
    switch (type)
    {
        case ESchemeParamType::Float:  return 4;
        case ESchemeParamType::Int:    return 4;
        case ESchemeParamType::UInt:   return 4;
        case ESchemeParamType::Float2: return 8;
        case ESchemeParamType::Float3: return 12;
        case ESchemeParamType::Float4: return 16;
    }
    return 4;
}


namespace
{
    bool ParseParamType(const String& text, ESchemeParamType& out)
    {
        if (text == "float")  { out = ESchemeParamType::Float;  return true; }
        if (text == "int")    { out = ESchemeParamType::Int;    return true; }
        if (text == "uint")   { out = ESchemeParamType::UInt;   return true; }
        if (text == "float2") { out = ESchemeParamType::Float2; return true; }
        if (text == "float3") { out = ESchemeParamType::Float3; return true; }
        if (text == "float4") { out = ESchemeParamType::Float4; return true; }
        return false;
    }

    glm::vec4 ParseDefaultValue(const nlohmann::json& j, ESchemeParamType type)
    {
        glm::vec4 value{ 0.0f };
        if (!j.contains("default"))
            return value;

        const nlohmann::json& def = j["default"];
        if (def.is_array())
        {
            uint32 count = std::min<uint32>((uint32)def.size(), 4u);
            for (uint32 i = 0; i < count; ++i)
                value[i] = def[i].get<float32>();
        }
        else if (def.is_number())
        {
            value.x = def.get<float32>();
        }

        (void)type;
        return value;
    }
}

const SchemeParam* MaterialScheme::FindParam(const String& paramName) const
{
    for (const SchemeParam& param : params)
    {
        if (param.name == paramName)
            return &param;
    }
    return nullptr;
}

// One 16-byte slot per parameter, in declaration order, however small the parameter is.
//
// This side has to agree byte-for-byte with a struct the scheme's shader declares by hand, and
// nothing can check that at compile time -- a disagreement just renders from the neighbouring
// field's bytes, or from the zero-filled gap between blocks. Rather than reproduce HLSL's packing
// rules for StructuredBuffer elements (which differ from cbuffer rules, are not obviously
// documented, and cost a full rebuild-and-look to test), give every parameter a full float4 slot:
// every member is then 16 bytes at a 16-byte offset, which *every* packing rule agrees on, and the
// stride is just 16 * count. There is nothing left for the two sides to disagree about.
//
// The waste is a few bytes per material slot per scheme -- kilobytes across a whole scene.
//
// The contract for a scheme's shader is therefore: one float4 (or int4) per JSON parameter, in the
// same order, reading scalars out of .x.
void MaterialScheme::ComputeLayout()
{
    for (uint32 i = 0; i < (uint32)params.Size(); ++i)
        params[i].byteOffset = i * kSchemeParamSlotSize;

    paramStride = (uint32)params.Size() * kSchemeParamSlotSize;
}

void MaterialScheme::PackParams(const TDictionary<String, glm::vec4>& values, uint8* dst) const
{
    if (paramStride == 0)
        return;

    std::memset(dst, 0, paramStride);

    for (const SchemeParam& param : params)
    {
        auto it = values.Find(param.name);
        glm::vec4 value = it != values.End() ? it->second : param.defaultValue;

        uint8* fieldDst = dst + param.byteOffset;
        switch (param.type)
        {
            case ESchemeParamType::Float:
            {
                float32 packed = value.x;
                std::memcpy(fieldDst, &packed, sizeof(packed));
                break;
            }
            case ESchemeParamType::Int:
            {
                int32 packed = (int32)value.x;
                std::memcpy(fieldDst, &packed, sizeof(packed));
                break;
            }
            case ESchemeParamType::UInt:
            {
                uint32 packed = (uint32)std::max(value.x, 0.0f);
                std::memcpy(fieldDst, &packed, sizeof(packed));
                break;
            }
            case ESchemeParamType::Float2:
            {
                float32 packed[2] = { value.x, value.y };
                std::memcpy(fieldDst, packed, sizeof(packed));
                break;
            }
            case ESchemeParamType::Float3:
            {
                float32 packed[3] = { value.x, value.y, value.z };
                std::memcpy(fieldDst, packed, sizeof(packed));
                break;
            }
            case ESchemeParamType::Float4:
            {
                float32 packed[4] = { value.x, value.y, value.z, value.w };
                std::memcpy(fieldDst, packed, sizeof(packed));
                break;
            }
        }
    }
}

void SchemeRegistry::LoadDirectory(const String& directory)
{
    namespace fs = std::filesystem;

    std::error_code error;
    if (!fs::is_directory(directory.CStr(), error))
    {
        CARAMEL_ERROR("SchemeRegistry: '{}' is not a directory -- no material schemes loaded", directory.CStr());
        return;
    }

    // The default scheme must claim id 0, so load it first regardless of directory order.
    String defaultPath = directory + "/" + kDefaultSchemeName + ".json";
    if (fs::exists(defaultPath.CStr(), error))
        LoadScheme(defaultPath);
    else
        CARAMEL_ERROR("SchemeRegistry: default scheme '{}' is missing -- scene geometry will not render", defaultPath.CStr());

    TArray<String> paths;
    for (const auto& entry : fs::directory_iterator(directory.CStr(), error))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        // Already loaded above to guarantee it owns id 0.
        if (entry.path().stem() == kDefaultSchemeName)
            continue;
        paths.PushBack(String(entry.path().string()));
    }

    // Stable order so scheme ids do not shuffle between runs (they are cached in gpuMaterialSlot
    // lookups and shown in the inspector).
    std::sort(paths.Begin(), paths.End());

    for (const String& path : paths)
        LoadScheme(path);

    CARAMEL_INFO("SchemeRegistry: loaded {} material scheme(s) from '{}'", m_Schemes.Size(), directory.CStr());
}

bool SchemeRegistry::LoadScheme(const String& path)
{
    std::ifstream file(path.CStr());
    if (!file)
    {
        CARAMEL_ERROR("SchemeRegistry: failed to open scheme '{}'", path.CStr());
        return false;
    }

    nlohmann::json j;
    try
    {
        file >> j;
    }
    catch (const nlohmann::json::exception& e)
    {
        CARAMEL_ERROR("SchemeRegistry: '{}' is not valid JSON: {}", path.CStr(), e.what());
        return false;
    }

    String name = String(j.value("name", ""));
    if (name.Empty())
    {
        CARAMEL_ERROR("SchemeRegistry: '{}' has no \"name\"", path.CStr());
        return false;
    }

    // Re-registering a shader path in ShaderServer is a clobbering overwrite that silently destroys
    // the compiled variant cache, so a duplicate name is refused rather than merged.
    if (m_ByName.Contains(name))
    {
        CARAMEL_WARN("SchemeRegistry: scheme '{}' is already loaded, ignoring '{}'", name.CStr(), path.CStr());
        return false;
    }

    String shaderPath = String(j.value("shader", ""));
    if (shaderPath.Empty())
    {
        CARAMEL_ERROR("SchemeRegistry: scheme '{}' has no \"shader\"", name.CStr());
        return false;
    }

    TUnique<MaterialScheme> scheme = MakeUnique<MaterialScheme>();
    scheme->name = name;
    scheme->shaderPath = String(std::filesystem::path(shaderPath.CStr()).generic_string());

    if (j.contains("flags"))
    {
        const nlohmann::json& flags = j["flags"];
        scheme->flags.castsShadows = flags.value("castsShadows", true);
        scheme->flags.receivesShadows = flags.value("receivesShadows", true);
    }

    for (const nlohmann::json& paramJson : j.value("parameters", nlohmann::json::array()))
    {
        SchemeParam param;
        param.name = String(paramJson.value("name", ""));
        if (param.name.Empty())
        {
            CARAMEL_WARN("SchemeRegistry: scheme '{}' has a parameter with no name, skipping", name.CStr());
            continue;
        }

        String typeText = String(paramJson.value("type", "float"));
        if (!ParseParamType(typeText, param.type))
        {
            CARAMEL_WARN("SchemeRegistry: scheme '{}' parameter '{}' has unsupported type '{}', skipping",
                         name.CStr(), param.name.CStr(), typeText.CStr());
            continue;
        }

        param.defaultValue = ParseDefaultValue(paramJson, param.type);
        param.minValue = paramJson.value("min", 0.0f);
        param.maxValue = paramJson.value("max", 1.0f);
        param.isColor = paramJson.value("color", false);

        scheme->params.PushBack(param);
    }
    scheme->ComputeLayout();

    // Safe because `scheme` is heap-owned and never moved: AGFX does not copy this string.
    scheme->pipelineTemplate.SetName(scheme->name.CStr());
    ShaderServer::RegisterComputePipeline(scheme->pipelineTemplate, scheme->shaderPath);

    if (!ShaderServer::GetComputePipeline(scheme->shaderPath, {}))
    {
        CARAMEL_ERROR("SchemeRegistry: scheme '{}' failed to compile '{}'", name.CStr(), scheme->shaderPath.CStr());
        return false;
    }

    uint32 schemeId = (uint32)m_Schemes.Size();
    if (schemeId >= kMaxSchemes)
    {
        CARAMEL_ERROR("SchemeRegistry: scheme '{}' exceeds the {}-scheme ceiling the material classification pass is sized for, ignoring",
                      name.CStr(), kMaxSchemes);
        return false;
    }

    m_ByName[name] = schemeId;
    m_Schemes.PushBack(std::move(scheme));

    const MaterialScheme& loaded = *m_Schemes[schemeId];
    CARAMEL_INFO("SchemeRegistry: scheme {} '{}' -> '{}' ({} param(s), {} byte stride)",
                 schemeId, name.CStr(), loaded.shaderPath.CStr(), loaded.params.Size(), loaded.paramStride);

    // These offsets have to match the parameter struct the scheme's shader declares by hand, member
    // for member -- nothing can check that at compile time, so log it. A scheme rendering from
    // plausible-but-wrong values is almost always a member declared in the wrong order here.
    for (const SchemeParam& param : loaded.params)
        CARAMEL_DEBUG("SchemeRegistry:   +{:<3} {}", param.byteOffset, param.name.CStr());

    return true;
}

uint32 SchemeRegistry::FindId(const String& schemeName) const
{
    if (schemeName.Empty())
        return kDefaultSchemeId;

    auto it = m_ByName.Find(schemeName);
    if (it != m_ByName.End())
        return it->second;

    return kDefaultSchemeId;
}

const MaterialScheme& SchemeRegistry::Get(uint32 schemeId) const
{
    if (schemeId >= m_Schemes.Size())
        schemeId = kDefaultSchemeId;
    return *m_Schemes[schemeId];
}
