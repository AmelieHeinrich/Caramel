/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <AGFX/agfx.hpp>
#include <glm/glm.hpp>

enum class ESchemeParamType
{
    Float,
    Int,
    UInt,
    Float2,
    Float3,
    Float4
};

uint32 SchemeParamSize(ESchemeParamType type);

struct SchemeParam
{
    String name;
    ESchemeParamType type = ESchemeParamType::Float;
    uint32 byteOffset = 0;

    glm::vec4 defaultValue{ 0.0f };     // scalars live in .x
    float32 minValue = 0.0f;
    float32 maxValue = 1.0f;
    bool isColor = false;               // draw as a color picker rather than drag floats
};

struct MaterialScheme
{
    String name;
    String shaderPath;

    agfx::RenderPipelineCreateInfo pipelineTemplate;

    struct
    {
        bool castsShadows = true;
        bool receivesShadows = true;
    } flags;

    TArray<SchemeParam> params;
    uint32 paramStride = 0;

    void ComputeLayout();

    void PackParams(const TDictionary<String, glm::vec4>& values, uint8* dst) const;

    const SchemeParam* FindParam(const String& paramName) const;
};

class SchemeRegistry
{
public:
    static constexpr uint32 kDefaultSchemeId = 0;
    static constexpr const char* kDefaultSchemeName = "DefaultPBR";

    void LoadDirectory(const String& directory, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat);
    uint32 FindId(const String& schemeName) const;

    const MaterialScheme& Get(uint32 schemeId) const;

    uint32 Count() const { return (uint32)m_Schemes.Size(); }

    bool IsEmpty() const { return m_Schemes.IsEmpty(); }

private:
    bool LoadScheme(const String& path, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat);

    TArray<TUnique<MaterialScheme>> m_Schemes;
    TDictionary<String, uint32> m_ByName;
};
