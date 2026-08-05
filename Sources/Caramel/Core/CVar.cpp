/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "CVar.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace
{
    String Trim(const String& s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == String::npos)
            return String();
        size_t end = s.find_last_not_of(" \t\r\n");
        return String(s.substr(start, end - start + 1));
    }

    TArray<String> Tokenize(const String& s)
    {
        TArray<String> tokens;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && std::isspace((unsigned char)s[i]))
                i++;
            size_t start = i;
            while (i < s.size() && !std::isspace((unsigned char)s[i]))
                i++;
            if (i > start)
                tokens.PushBack(String(s.substr(start, i - start)));
        }
        return tokens;
    }
}

CVar::CVar(const char* name, bool defaultValue, const char* displayName, const char* category, const char* description)
    : m_Name(name), m_DisplayName(displayName), m_Category(category), m_Description(description), m_Type(ECVarType::Bool)
{
    m_BoolValue = defaultValue;
    m_DefaultBool = defaultValue;
    Register();
}

CVar::CVar(const char* name, int32 defaultValue, int32 minValue, int32 maxValue, const char* displayName, const char* category, const char* description)
    : m_Name(name), m_DisplayName(displayName), m_Category(category), m_Description(description), m_Type(ECVarType::Int)
{
    m_IntValue = defaultValue;
    m_DefaultInt = defaultValue;
    m_MinInt = minValue;
    m_MaxInt = maxValue;
    Register();
}

CVar::CVar(const char* name, float defaultValue, float minValue, float maxValue, const char* displayName, const char* category, const char* description)
    : m_Name(name), m_DisplayName(displayName), m_Category(category), m_Description(description), m_Type(ECVarType::Float)
{
    m_FloatValue = defaultValue;
    m_DefaultFloat = defaultValue;
    m_MinFloat = minValue;
    m_MaxFloat = maxValue;
    Register();
}

CVar::CVar(const char* name, const char* defaultValue, const char* displayName, const char* category, const char* description)
    : m_Name(name), m_DisplayName(displayName), m_Category(category), m_Description(description), m_Type(ECVarType::String)
{
    m_StringValue = defaultValue;
    m_DefaultString = defaultValue;
    Register();
}

void CVar::Register()
{
    CVar*& head = CVarRegistry::Head();
    m_Next = head;
    head = this;
}

String CVar::ToString() const
{
    switch (m_Type) {
        case ECVarType::Bool:
            return m_BoolValue ? "true" : "false";
        case ECVarType::Int:
            return String(std::to_string(m_IntValue));
        case ECVarType::Float:
            return String(std::to_string(m_FloatValue));
        case ECVarType::String:
            return m_StringValue;
    }
    return String();
}

bool CVar::SetFromString(const String& value)
{
    switch (m_Type) {
        case ECVarType::Bool: {
            String v = value;
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (v == "1" || v == "true") {
                m_BoolValue = true;
                return true;
            }
            if (v == "0" || v == "false") {
                m_BoolValue = false;
                return true;
            }
            return false;
        }
        case ECVarType::Int: {
            try {
                size_t consumed = 0;
                int32 v = std::stoi(value, &consumed);
                if (consumed == 0)
                    return false;
                if (m_MaxInt > m_MinInt)
                    v = std::clamp(v, m_MinInt, m_MaxInt);
                m_IntValue = v;
                return true;
            } catch (...) {
                return false;
            }
        }
        case ECVarType::Float: {
            try {
                size_t consumed = 0;
                float v = std::stof(value, &consumed);
                if (consumed == 0)
                    return false;
                if (m_MaxFloat > m_MinFloat)
                    v = std::clamp(v, m_MinFloat, m_MaxFloat);
                m_FloatValue = v;
                return true;
            } catch (...) {
                return false;
            }
        }
        case ECVarType::String:
            m_StringValue = value;
            return true;
    }
    return false;
}

void CVar::ResetToDefault()
{
    switch (m_Type) {
        case ECVarType::Bool:   m_BoolValue = m_DefaultBool; break;
        case ECVarType::Int:    m_IntValue = m_DefaultInt; break;
        case ECVarType::Float:  m_FloatValue = m_DefaultFloat; break;
        case ECVarType::String: m_StringValue = m_DefaultString; break;
    }
}

ConsoleCommand::ConsoleCommand(const char* name, const char* category, const char* description, Callback callback)
    : m_Name(name), m_Category(category), m_Description(description), m_Callback(std::move(callback))
{
    ConsoleCommand*& head = CVarRegistry::CommandHead();
    m_Next = head;
    head = this;
}

CVar*& CVarRegistry::Head()
{
    static CVar* head = nullptr;
    return head;
}

ConsoleCommand*& CVarRegistry::CommandHead()
{
    static ConsoleCommand* head = nullptr;
    return head;
}

CVar* CVarRegistry::Find(const String& name)
{
    for (CVar* cvar = Head(); cvar; cvar = cvar->GetNext())
        if (cvar->GetName() == name)
            return cvar;
    return nullptr;
}

ConsoleCommand* CVarRegistry::FindCommand(const String& name)
{
    for (ConsoleCommand* cmd = CommandHead(); cmd; cmd = cmd->GetNext())
        if (cmd->GetName() == name)
            return cmd;
    return nullptr;
}

TArray<CVar*> CVarRegistry::All()
{
    TArray<CVar*> result;
    for (CVar* cvar = Head(); cvar; cvar = cvar->GetNext())
        result.PushBack(cvar);
    return result;
}

TArray<ConsoleCommand*> CVarRegistry::AllCommands()
{
    TArray<ConsoleCommand*> result;
    for (ConsoleCommand* cmd = CommandHead(); cmd; cmd = cmd->GetNext())
        result.PushBack(cmd);
    return result;
}

TArray<String> CVarRegistry::Autocomplete(const String& prefix)
{
    TArray<String> result;
    for (CVar* cvar = Head(); cvar; cvar = cvar->GetNext())
        if (cvar->GetName().rfind(prefix, 0) == 0)
            result.PushBack(cvar->GetName());
    for (ConsoleCommand* cmd = CommandHead(); cmd; cmd = cmd->GetNext())
        if (cmd->GetName().rfind(prefix, 0) == 0)
            result.PushBack(cmd->GetName());
    std::sort(result.Begin(), result.End());
    return result;
}

void CVarRegistry::ExecuteLine(const String& line, TArray<String>& outLines)
{
    String trimmed = Trim(line);
    if (trimmed.Empty())
        return;

    size_t sp = trimmed.find_first_of(" \t");
    String name = sp == String::npos ? trimmed : String(trimmed.substr(0, sp));
    String remainder = sp == String::npos ? String() : Trim(String(trimmed.substr(sp + 1)));

    if (CVar* cvar = Find(name)) {
        if (!remainder.Empty()) {
            if (!cvar->SetFromString(remainder)) {
                outLines.PushBack(name + ": invalid value '" + remainder + "'");
                return;
            }
        }
        outLines.PushBack(name + " = " + cvar->ToString());
        return;
    }

    if (ConsoleCommand* cmd = FindCommand(name)) {
        TArray<String> args = Tokenize(remainder);
        cmd->Invoke(args);
        return;
    }

    outLines.PushBack("Unknown command: " + name);
}

void CVarRegistry::SaveToFile(const String& path)
{
    nlohmann::json j;
    for (CVar* cvar = Head(); cvar; cvar = cvar->GetNext()) {
        switch (cvar->GetType()) {
            case ECVarType::Bool:
                j[cvar->GetName().CStr()] = *cvar->AsBoolPtr();
                break;
            case ECVarType::Int:
                j[cvar->GetName().CStr()] = *cvar->AsIntPtr();
                break;
            case ECVarType::Float:
                j[cvar->GetName().CStr()] = *cvar->AsFloatPtr();
                break;
            case ECVarType::String:
                j[cvar->GetName().CStr()] = cvar->AsStringPtr()->CStr();
                break;
        }
    }

    std::ofstream file(path.CStr());
    if (!file.is_open())
        return;
    file << j.dump(4);
}

void CVarRegistry::ResetAllToDefaults()
{
    for (CVar* cvar = Head(); cvar; cvar = cvar->GetNext())
        cvar->ResetToDefault();
}

void CVarRegistry::LoadFromFile(const String& path)
{
    std::ifstream file(path.CStr());
    if (!file.is_open())
        return;

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        return;
    }

    for (CVar* cvar = Head(); cvar; cvar = cvar->GetNext()) {
        auto it = j.find(cvar->GetName().CStr());
        if (it == j.end())
            continue;

        switch (cvar->GetType()) {
            case ECVarType::Bool:
                if (it->is_boolean())
                    *cvar->AsBoolPtr() = it->get<bool>();
                break;
            case ECVarType::Int:
                if (it->is_number_integer())
                    *cvar->AsIntPtr() = it->get<int32>();
                break;
            case ECVarType::Float:
                if (it->is_number())
                    *cvar->AsFloatPtr() = it->get<float>();
                break;
            case ECVarType::String:
                if (it->is_string())
                    *cvar->AsStringPtr() = String(it->get<std::string>());
                break;
        }
    }
}
