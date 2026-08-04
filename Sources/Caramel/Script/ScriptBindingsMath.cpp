/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Script/ScriptBindings.hpp>
#include <Caramel/Core/Common.hpp>

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <new>

namespace
{
    void Vec2DefaultCtor(void* memory) { new (memory) glm::vec2(0.0f); }
    void Vec2Ctor1(float32 value, void* memory) { new (memory) glm::vec2(value); }
    void Vec2Ctor2(float32 x, float32 y, void* memory) { new (memory) glm::vec2(x, y); }

    void Vec3DefaultCtor(void* memory) { new (memory) glm::vec3(0.0f); }
    void Vec3Ctor1(float32 value, void* memory) { new (memory) glm::vec3(value); }
    void Vec3Ctor3(float32 x, float32 y, float32 z, void* memory) { new (memory) glm::vec3(x, y, z); }

    void Vec4DefaultCtor(void* memory) { new (memory) glm::vec4(0.0f); }
    void Vec4Ctor1(float32 value, void* memory) { new (memory) glm::vec4(value); }
    void Vec4Ctor4(float32 x, float32 y, float32 z, float32 w, void* memory) { new (memory) glm::vec4(x, y, z, w); }
    void Vec4CtorVec3(const glm::vec3& xyz, float32 w, void* memory) { new (memory) glm::vec4(xyz, w); }

    void Mat4DefaultCtor(void* memory) { new (memory) glm::mat4(1.0f); }
    void Mat4Ctor1(float32 diagonal, void* memory) { new (memory) glm::mat4(diagonal); }

    template<typename T>
    T VecAdd(const T& rhs, const T* self) { return *self + rhs; }

    template<typename T>
    T VecSub(const T& rhs, const T* self) { return *self - rhs; }

    template<typename T>
    T VecMulVec(const T& rhs, const T* self) { return *self * rhs; }

    template<typename T>
    T VecMulScalar(float32 rhs, const T* self) { return *self * rhs; }

    template<typename T>
    T VecDivScalar(float32 rhs, const T* self) { return *self / rhs; }

    template<typename T>
    T VecNeg(const T* self) { return -*self; }

    template<typename T>
    bool VecEquals(const T& rhs, const T* self) { return *self == rhs; }

    template<typename T>
    T& VecAddAssign(const T& rhs, T* self) { *self += rhs; return *self; }

    template<typename T>
    T& VecSubAssign(const T& rhs, T* self) { *self -= rhs; return *self; }

    template<typename T>
    T& VecMulAssign(float32 rhs, T* self) { *self *= rhs; return *self; }

    template<typename T>
    float32 VecDot(const T& a, const T& b) { return glm::dot(a, b); }

    template<typename T>
    float32 VecLength(const T& value) { return glm::length(value); }

    template<typename T>
    T VecNormalize(const T& value)
    {
        float32 lengthSquared = glm::dot(value, value);
        if (lengthSquared <= 0.0f)
            return T(0.0f);
        return value / glm::sqrt(lengthSquared);
    }

    template<typename T>
    T VecLerp(const T& a, const T& b, float32 t) { return a + (b - a) * t; }

    glm::vec3 Vec3Cross(const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); }

    glm::mat4 Mat4MulMat4(const glm::mat4& rhs, const glm::mat4* self) { return *self * rhs; }
    glm::vec4 Mat4MulVec4(const glm::vec4& rhs, const glm::mat4* self) { return *self * rhs; }

    glm::mat4 MakeTranslate(const glm::vec3& value) { return glm::translate(glm::mat4(1.0f), value); }
    glm::mat4 MakeScale(const glm::vec3& value) { return glm::scale(glm::mat4(1.0f), value); }

    glm::mat4 MakeRotateEuler(const glm::vec3& degrees)
    {
        return glm::eulerAngleXYZ(glm::radians(degrees.x), glm::radians(degrees.y), glm::radians(degrees.z));
    }

    template<typename T>
    void RegisterVectorCommon(asIScriptEngine* engine, const char* name)
    {
        String decl;

        decl = String(name) + " opAdd(const " + name + " &in) const";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecAdd<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + " opSub(const " + name + " &in) const";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecSub<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + " opMul(const " + name + " &in) const";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecMulVec<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + " opMul(float) const";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecMulScalar<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + " opMul_r(float) const";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecMulScalar<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + " opDiv(float) const";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecDivScalar<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + " opNeg() const";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecNeg<T>), asCALL_CDECL_OBJLAST);

        decl = String("bool opEquals(const ") + name + " &in) const";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecEquals<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + "& opAddAssign(const " + name + " &in)";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecAddAssign<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + "& opSubAssign(const " + name + " &in)";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecSubAssign<T>), asCALL_CDECL_OBJLAST);

        decl = String(name) + "& opMulAssign(float)";
        engine->RegisterObjectMethod(name, decl.CStr(), asFUNCTION(VecMulAssign<T>), asCALL_CDECL_OBJLAST);

        decl = String("float dot(const ") + name + " &in, const " + name + " &in)";
        engine->RegisterGlobalFunction(decl.CStr(), asFUNCTION(VecDot<T>), asCALL_CDECL);

        decl = String("float length(const ") + name + " &in)";
        engine->RegisterGlobalFunction(decl.CStr(), asFUNCTION(VecLength<T>), asCALL_CDECL);

        decl = String(name) + " normalize(const " + name + " &in)";
        engine->RegisterGlobalFunction(decl.CStr(), asFUNCTION(VecNormalize<T>), asCALL_CDECL);

        decl = String(name) + " lerp(const " + name + " &in, const " + name + " &in, float)";
        engine->RegisterGlobalFunction(decl.CStr(), asFUNCTION(VecLerp<T>), asCALL_CDECL);
    }
}

void RegisterMathTypes(asIScriptEngine* engine)
{
    const asDWORD vectorFlags = asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS;

    engine->RegisterObjectType("vec2", sizeof(glm::vec2), vectorFlags | asGetTypeTraits<glm::vec2>());
    engine->RegisterObjectProperty("vec2", "float x", asOFFSET(glm::vec2, x));
    engine->RegisterObjectProperty("vec2", "float y", asOFFSET(glm::vec2, y));
    engine->RegisterObjectBehaviour("vec2", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(Vec2DefaultCtor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("vec2", asBEHAVE_CONSTRUCT, "void f(float)", asFUNCTION(Vec2Ctor1), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("vec2", asBEHAVE_CONSTRUCT, "void f(float, float)", asFUNCTION(Vec2Ctor2), asCALL_CDECL_OBJLAST);

    engine->RegisterObjectType("vec3", sizeof(glm::vec3), vectorFlags | asGetTypeTraits<glm::vec3>());
    engine->RegisterObjectProperty("vec3", "float x", asOFFSET(glm::vec3, x));
    engine->RegisterObjectProperty("vec3", "float y", asOFFSET(glm::vec3, y));
    engine->RegisterObjectProperty("vec3", "float z", asOFFSET(glm::vec3, z));
    engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(Vec3DefaultCtor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f(float)", asFUNCTION(Vec3Ctor1), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f(float, float, float)", asFUNCTION(Vec3Ctor3), asCALL_CDECL_OBJLAST);

    engine->RegisterObjectType("vec4", sizeof(glm::vec4), vectorFlags | asGetTypeTraits<glm::vec4>());
    engine->RegisterObjectProperty("vec4", "float x", asOFFSET(glm::vec4, x));
    engine->RegisterObjectProperty("vec4", "float y", asOFFSET(glm::vec4, y));
    engine->RegisterObjectProperty("vec4", "float z", asOFFSET(glm::vec4, z));
    engine->RegisterObjectProperty("vec4", "float w", asOFFSET(glm::vec4, w));
    engine->RegisterObjectBehaviour("vec4", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(Vec4DefaultCtor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("vec4", asBEHAVE_CONSTRUCT, "void f(float)", asFUNCTION(Vec4Ctor1), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("vec4", asBEHAVE_CONSTRUCT, "void f(float, float, float, float)", asFUNCTION(Vec4Ctor4), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("vec4", asBEHAVE_CONSTRUCT, "void f(const vec3 &in, float)", asFUNCTION(Vec4CtorVec3), asCALL_CDECL_OBJLAST);

    RegisterVectorCommon<glm::vec2>(engine, "vec2");
    RegisterVectorCommon<glm::vec3>(engine, "vec3");
    RegisterVectorCommon<glm::vec4>(engine, "vec4");

    engine->RegisterGlobalFunction("vec3 cross(const vec3 &in, const vec3 &in)", asFUNCTION(Vec3Cross), asCALL_CDECL);

    engine->RegisterObjectType("mat4", sizeof(glm::mat4), vectorFlags | asGetTypeTraits<glm::mat4>());
    engine->RegisterObjectBehaviour("mat4", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(Mat4DefaultCtor), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectBehaviour("mat4", asBEHAVE_CONSTRUCT, "void f(float)", asFUNCTION(Mat4Ctor1), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("mat4", "mat4 opMul(const mat4 &in) const", asFUNCTION(Mat4MulMat4), asCALL_CDECL_OBJLAST);
    engine->RegisterObjectMethod("mat4", "vec4 opMul(const vec4 &in) const", asFUNCTION(Mat4MulVec4), asCALL_CDECL_OBJLAST);

    engine->RegisterGlobalFunction("mat4 translate(const vec3 &in)", asFUNCTION(MakeTranslate), asCALL_CDECL);
    engine->RegisterGlobalFunction("mat4 scaleMat(const vec3 &in)", asFUNCTION(MakeScale), asCALL_CDECL);
    engine->RegisterGlobalFunction("mat4 rotateEuler(const vec3 &in)", asFUNCTION(MakeRotateEuler), asCALL_CDECL);
}
