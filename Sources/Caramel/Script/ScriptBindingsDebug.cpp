/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Script/ScriptBindings.hpp>
#include <Caramel/Script/ScriptTime.hpp>
#include <Caramel/Renderer/DebugRenderer.hpp>
#include <Caramel/Core/Input.hpp>
#include <Caramel/Core/Logger.hpp>

#include <imgui.h>

#include <new>
#include <string>

namespace
{
    void DebugStyleCtor(void* memory) { new (memory) DebugStyle(); }
    void DebugStyleDtor(void* memory) { ((DebugStyle*)memory)->~DebugStyle(); }

    void Debug_Line(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style)
    {
        DebugRenderer::Get().Line(a, b, style);
    }

    void Debug_Point(const glm::vec3& p, const DebugStyle& style)
    {
        DebugRenderer::Get().Point(p, style);
    }

    void Debug_QuadTransform(const glm::mat4& transform, const glm::vec2& halfExtents, const DebugStyle& style)
    {
        DebugRenderer::Get().Quad(transform, halfExtents, style);
    }

    void Debug_QuadBasis(const glm::vec3& center, const glm::vec3& right, const glm::vec3& up, const DebugStyle& style)
    {
        DebugRenderer::Get().Quad(center, right, up, style);
    }

    void Debug_Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const DebugStyle& style)
    {
        DebugRenderer::Get().Triangle(a, b, c, style);
    }

    void Debug_Box(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const DebugStyle& style)
    {
        DebugRenderer::Get().Box(boundsMin, boundsMax, style);
    }

    void Debug_Arrow(const glm::vec3& from, const glm::vec3& to, const DebugStyle& style, float32 headLength, float32 headRadius)
    {
        DebugRenderer::Get().Arrow(from, to, style, headLength, headRadius);
    }

    void Debug_CylinderAxis(const glm::vec3& base, const glm::vec3& axis, float32 radius, const DebugStyle& style)
    {
        DebugRenderer::Get().Cylinder(base, axis, radius, style);
    }

    void Debug_CylinderTransform(const glm::mat4& transform, float32 radius, float32 height, const DebugStyle& style)
    {
        DebugRenderer::Get().Cylinder(transform, radius, height, style);
    }

    void Debug_CapsulePoints(const glm::vec3& a, const glm::vec3& b, float32 radius, const DebugStyle& style)
    {
        DebugRenderer::Get().Capsule(a, b, radius, style);
    }

    void Debug_CapsuleTransform(const glm::mat4& transform, float32 radius, float32 cylinderHeight, const DebugStyle& style)
    {
        DebugRenderer::Get().Capsule(transform, radius, cylinderHeight, style);
    }

    void Debug_ConeDirection(const glm::vec3& apex, const glm::vec3& dir, float32 height, float32 radius, const DebugStyle& style)
    {
        DebugRenderer::Get().Cone(apex, dir, height, radius, style);
    }

    void Debug_ConeTransform(const glm::mat4& transform, float32 radius, float32 height, const DebugStyle& style)
    {
        DebugRenderer::Get().Cone(transform, radius, height, style);
    }

    void Debug_SphereRings(const glm::vec3& center, float32 radius, const DebugStyle& style)
    {
        DebugRenderer::Get().SphereRings(center, radius, style);
    }

    void Debug_Sphere(const glm::vec3& center, float32 radius, const DebugStyle& style)
    {
        DebugRenderer::Get().Sphere(center, radius, style);
    }

    void Debug_Axes(const glm::mat4& transform, float32 scale, const DebugStyle& style)
    {
        DebugRenderer::Get().Axes(transform, scale, style);
    }

    void Debug_Frustum(const glm::mat4& viewProj, const DebugStyle& style)
    {
        DebugRenderer::Get().Frustum(viewProj, style);
    }

    float32 Time_GetDelta() { return ScriptTime::GetDelta(); }
    float32 Time_GetElapsed() { return ScriptTime::GetElapsed(); }
    uint32 Time_GetFrame() { return (uint32)ScriptTime::GetFrameCount(); }

    bool ScriptsOwnKeyboard()
    {
        return !ImGui::GetIO().WantCaptureKeyboard && !ImGui::GetIO().WantTextInput;
    }

    bool Input_IsKeyDown(int32 scancode)
    {
        if (!ScriptsOwnKeyboard())
            return false;
        return Input::IsKeyDown((SDL_Scancode)scancode);
    }

    bool Input_IsKeyPressed(int32 scancode)
    {
        if (!ScriptsOwnKeyboard())
            return false;
        return Input::IsKeyPressed((SDL_Scancode)scancode);
    }

    glm::vec2 Input_GetMousePosition() { return Input::GetMousePosition(); }
    glm::vec2 Input_GetMouseDelta() { return Input::GetMouseDelta(); }

    void Script_Print(const std::string& message) { CARAMEL_INFO("[Script] {}", message); }
    void Script_PrintWarn(const std::string& message) { CARAMEL_WARN("[Script] {}", message); }
    void Script_PrintError(const std::string& message) { CARAMEL_ERROR("[Script] {}", message); }
}

void RegisterDebugAPI(asIScriptEngine* engine)
{
    engine->RegisterObjectType("DebugStyle", sizeof(DebugStyle), asOBJ_VALUE | asGetTypeTraits<DebugStyle>());
    engine->RegisterObjectBehaviour("DebugStyle", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(DebugStyleCtor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("DebugStyle", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(DebugStyleDtor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectProperty("DebugStyle", "vec4 color", asOFFSET(DebugStyle, color));
    engine->RegisterObjectProperty("DebugStyle", "bool filled", asOFFSET(DebugStyle, filled));
    engine->RegisterObjectProperty("DebugStyle", "bool depthTest", asOFFSET(DebugStyle, depthTest));
    engine->RegisterObjectProperty("DebugStyle", "float thickness", asOFFSET(DebugStyle, thickness));
    engine->RegisterObjectProperty("DebugStyle", "uint segments", asOFFSET(DebugStyle, segments));

    engine->SetDefaultNamespace("Debug");
    engine->RegisterGlobalFunction("void Line(const vec3 &in, const vec3 &in, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_Line), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Point(const vec3 &in, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_Point), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Quad(const mat4 &in, const vec2 &in, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_QuadTransform), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Quad(const vec3 &in, const vec3 &in, const vec3 &in, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_QuadBasis), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Triangle(const vec3 &in, const vec3 &in, const vec3 &in, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_Triangle), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Box(const vec3 &in, const vec3 &in, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_Box), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Arrow(const vec3 &in, const vec3 &in, const DebugStyle &in style = DebugStyle(), float headLength = -1, float headRadius = -1)", asFUNCTION(Debug_Arrow), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Cylinder(const vec3 &in, const vec3 &in, float, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_CylinderAxis), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Cylinder(const mat4 &in, float, float, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_CylinderTransform), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Capsule(const vec3 &in, const vec3 &in, float, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_CapsulePoints), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Capsule(const mat4 &in, float, float, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_CapsuleTransform), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Cone(const vec3 &in, const vec3 &in, float, float, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_ConeDirection), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Cone(const mat4 &in, float, float, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_ConeTransform), asCALL_CDECL);
    engine->RegisterGlobalFunction("void SphereRings(const vec3 &in, float, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_SphereRings), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Sphere(const vec3 &in, float, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_Sphere), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Axes(const mat4 &in, float scale = 1.0f, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_Axes), asCALL_CDECL);
    engine->RegisterGlobalFunction("void Frustum(const mat4 &in, const DebugStyle &in style = DebugStyle())", asFUNCTION(Debug_Frustum), asCALL_CDECL);
    engine->SetDefaultNamespace("");

    engine->SetDefaultNamespace("Time");
    engine->RegisterGlobalFunction("float get_delta() property", asFUNCTION(Time_GetDelta), asCALL_CDECL);
    engine->RegisterGlobalFunction("float get_elapsed() property", asFUNCTION(Time_GetElapsed), asCALL_CDECL);
    engine->RegisterGlobalFunction("uint get_frame() property", asFUNCTION(Time_GetFrame), asCALL_CDECL);
    engine->SetDefaultNamespace("");

    engine->SetDefaultNamespace("Key");
    engine->RegisterGlobalProperty("const int W", (void*)&kScriptKeyW);
    engine->RegisterGlobalProperty("const int A", (void*)&kScriptKeyA);
    engine->RegisterGlobalProperty("const int S", (void*)&kScriptKeyS);
    engine->RegisterGlobalProperty("const int D", (void*)&kScriptKeyD);
    engine->RegisterGlobalProperty("const int Q", (void*)&kScriptKeyQ);
    engine->RegisterGlobalProperty("const int E", (void*)&kScriptKeyE);
    engine->RegisterGlobalProperty("const int Space", (void*)&kScriptKeySpace);
    engine->RegisterGlobalProperty("const int LeftShift", (void*)&kScriptKeyLeftShift);
    engine->RegisterGlobalProperty("const int Left", (void*)&kScriptKeyLeft);
    engine->RegisterGlobalProperty("const int Right", (void*)&kScriptKeyRight);
    engine->RegisterGlobalProperty("const int Up", (void*)&kScriptKeyUp);
    engine->RegisterGlobalProperty("const int Down", (void*)&kScriptKeyDown);
    engine->SetDefaultNamespace("");

    engine->SetDefaultNamespace("Input");
    engine->RegisterGlobalFunction("bool IsKeyDown(int)", asFUNCTION(Input_IsKeyDown), asCALL_CDECL);
    engine->RegisterGlobalFunction("bool IsKeyPressed(int)", asFUNCTION(Input_IsKeyPressed), asCALL_CDECL);
    engine->RegisterGlobalFunction("vec2 GetMousePosition()", asFUNCTION(Input_GetMousePosition), asCALL_CDECL);
    engine->RegisterGlobalFunction("vec2 GetMouseDelta()", asFUNCTION(Input_GetMouseDelta), asCALL_CDECL);
    engine->SetDefaultNamespace("");

    engine->RegisterGlobalFunction("void Print(const string &in)", asFUNCTION(Script_Print), asCALL_CDECL);
    engine->RegisterGlobalFunction("void PrintWarn(const string &in)", asFUNCTION(Script_PrintWarn), asCALL_CDECL);
    engine->RegisterGlobalFunction("void PrintError(const string &in)", asFUNCTION(Script_PrintError), asCALL_CDECL);
}
