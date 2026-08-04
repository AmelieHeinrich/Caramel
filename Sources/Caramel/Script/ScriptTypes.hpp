/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <glm/glm.hpp>

enum class EScriptPropertyType
{
    Bool,
    Int,
    Float,
    Float2,
    Float3,
    Float4,
    Text
};

enum class EScriptScope
{
    Node,
    Instance,
    Mesh
};

struct ScriptProperty
{
    String name;
    EScriptPropertyType type = EScriptPropertyType::Float;
    uint32 propertyIndex = 0;
    int32 typeId = 0;

    glm::vec4 defaultValue{ 0.0f };
    String defaultText;

    bool hasRange = false;
    float32 minValue = 0.0f;
    float32 maxValue = 1.0f;
    bool isColor = false;

    // Hidden properties are still snapshotted and restored across a hot reload -- that is how a
    // script keeps latched state (a captured origin, a "has spawned" flag) from resetting on edit.
    bool hidden = false;

    String tooltip;
};

struct ScriptComponent
{
    String scriptPath;
    String className;

    EScriptScope scope = EScriptScope::Node;
    uint32 targetIndex = 0;

    TDictionary<String, glm::vec4> propertyValues;
    TDictionary<String, String> textPropertyValues;

    bool enabled = true;
};

inline const char* ScriptScopeToString(EScriptScope scope)
{
    switch (scope) {
        case EScriptScope::Instance: return "instance";
        case EScriptScope::Mesh:     return "mesh";
        default:                     return "node";
    }
}

inline EScriptScope ScriptScopeFromString(const String& value)
{
    if (value == "instance")
        return EScriptScope::Instance;
    if (value == "mesh")
        return EScriptScope::Mesh;
    return EScriptScope::Node;
}
