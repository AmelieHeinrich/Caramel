/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <functional>

enum class ECVarType
{
    Bool,
    Int,
    Float,
    String
};

// A named, typed, self-registering global value -- declare one as a file-scope static from
// anywhere in the engine (Source/Quake ConVar style) and it is immediately visible to both the
// dev console (as "name value") and the Settings panel (as a grouped widget), with no separate
// registration call needed.
class CVar
{
public:
    CVar(const char* name, bool defaultValue, const char* displayName, const char* category, const char* description);
    CVar(const char* name, int32 defaultValue, int32 minValue, int32 maxValue, const char* displayName, const char* category, const char* description);
    CVar(const char* name, float defaultValue, float minValue, float maxValue, const char* displayName, const char* category, const char* description);
    CVar(const char* name, const char* defaultValue, const char* displayName, const char* category, const char* description);

    const String& GetName() const { return m_Name; }
    const String& GetDisplayName() const { return m_DisplayName; }
    const String& GetCategory() const { return m_Category; }
    const String& GetDescription() const { return m_Description; }
    ECVarType GetType() const { return m_Type; }

    bool* AsBoolPtr() { return &m_BoolValue; }
    int32* AsIntPtr() { return &m_IntValue; }
    float* AsFloatPtr() { return &m_FloatValue; }
    String* AsStringPtr() { return &m_StringValue; }

    int32 GetMinInt() const { return m_MinInt; }
    int32 GetMaxInt() const { return m_MaxInt; }
    float GetMinFloat() const { return m_MinFloat; }
    float GetMaxFloat() const { return m_MaxFloat; }

    String ToString() const;
    bool SetFromString(const String& value);

    /// @brief Restores the value this CVar was constructed with.
    void ResetToDefault();

    CVar* GetNext() const { return m_Next; }

private:
    void Register();

    String m_Name;
    String m_DisplayName;
    String m_Category;
    String m_Description;
    ECVarType m_Type;

    bool m_BoolValue = false;
    int32 m_IntValue = 0;
    float m_FloatValue = 0.0f;
    String m_StringValue;

    // Set once at construction, never mutated afterward -- what ResetToDefault() restores.
    bool m_DefaultBool = false;
    int32 m_DefaultInt = 0;
    float m_DefaultFloat = 0.0f;
    String m_DefaultString;

    int32 m_MinInt = 0;
    int32 m_MaxInt = 0;
    float m_MinFloat = 0.0f;
    float m_MaxFloat = 0.0f;

    CVar* m_Next = nullptr;

    friend class CVarRegistry;
};

// A named, self-registering command -- same intrusive-list pattern as CVar, for actions that
// aren't a single stored value (e.g. "shader.reload").
class ConsoleCommand
{
public:
    using Callback = std::function<void(const TArray<String>& args)>;

    ConsoleCommand(const char* name, const char* category, const char* description, Callback callback);

    const String& GetName() const { return m_Name; }
    const String& GetCategory() const { return m_Category; }
    const String& GetDescription() const { return m_Description; }

    void Invoke(const TArray<String>& args) const { m_Callback(args); }

    ConsoleCommand* GetNext() const { return m_Next; }

private:
    String m_Name;
    String m_Category;
    String m_Description;
    Callback m_Callback;

    ConsoleCommand* m_Next = nullptr;

    friend class CVarRegistry;
};

// Static registry over every CVar/ConsoleCommand ever constructed. Backed by intrusive linked
// lists populated from the CVar/ConsoleCommand constructors, so registration works regardless of
// static-initialization order across translation units.
class CVarRegistry
{
public:
    static CVar* Find(const String& name);
    static ConsoleCommand* FindCommand(const String& name);

    static TArray<CVar*> All();
    static TArray<ConsoleCommand*> AllCommands();

    // Prefix match over CVar and command names, sorted alphabetically.
    static TArray<String> Autocomplete(const String& prefix);

    // Parses "name" (print current value), "name value..." (set), or a registered command name
    // followed by its arguments. Appends one or more human-readable lines to outLines.
    static void ExecuteLine(const String& line, TArray<String>& outLines);

    static void SaveToFile(const String& path);
    static void LoadFromFile(const String& path);

    /// @brief Restores every registered CVar to the value it was constructed with.
    static void ResetAllToDefaults();

private:
    static CVar*& Head();
    static ConsoleCommand*& CommandHead();

    friend class CVar;
    friend class ConsoleCommand;
};
