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

/// @brief Bytes one scheme parameter occupies in the per-scheme parameter buffer, whatever its type.
/// A full float4 slot each, so the CPU-side layout and the struct a scheme's shader declares by hand
/// cannot disagree about packing -- see MaterialScheme::ComputeLayout.
constexpr uint32 kSchemeParamSlotSize = 16;

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

    // Schemes are deferred shading passes replayed from a Dispatch indirect bundle, not raster
    // pipelines -- the visibility buffer owns every piece of raster state now.
    agfx::ComputePipelineCreateInfo pipelineTemplate;

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

    // Hard ceiling on scheme count: the material classification pass keeps one count/offset/cursor
    // slot and one indirect-dispatch bundle region per scheme, and its shaders size their
    // groupshared arrays from this. Mirrors kMaxShadingSchemes in Common/DeferredShading.hlsli.
    static constexpr uint32 kMaxSchemes = 8;

    void LoadDirectory(const String& directory);
    uint32 FindId(const String& schemeName) const;

    const MaterialScheme& Get(uint32 schemeId) const;

    uint32 Count() const { return (uint32)m_Schemes.Size(); }

    bool IsEmpty() const { return m_Schemes.IsEmpty(); }

private:
    bool LoadScheme(const String& path);

    TArray<TUnique<MaterialScheme>> m_Schemes;
    TDictionary<String, uint32> m_ByName;
};
