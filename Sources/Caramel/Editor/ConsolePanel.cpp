/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ConsolePanel.hpp"

#include <Caramel/Core/CVar.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kHeightFraction = 0.4f;
    constexpr float kAnimSpeed = 12.0f;
    constexpr size_t kMaxVisibleMatches = 8;
}

void ConsolePanel::SetOpen(bool open)
{
    m_TargetOpen = open;
    if (open)
        m_FocusRequested = true;
}

void ConsolePanel::Draw(float deltaTime)
{
    float target = m_TargetOpen ? 1.0f : 0.0f;
    m_OpenAmount += (target - m_OpenAmount) * std::min(deltaTime * kAnimSpeed, 1.0f);
    if (std::fabs(target - m_OpenAmount) < 0.001f)
        m_OpenAmount = target;

    if (m_OpenAmount <= 0.0f)
        return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float height = viewport->Size.y * kHeightFraction;
    float eased = m_OpenAmount * m_OpenAmount * (3.0f - 2.0f * m_OpenAmount); // smoothstep
    float y = viewport->Pos.y - height + height * eased;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Console", nullptr, flags);

    float reservedForInput = ImGui::GetFrameHeightWithSpacing();
    float reservedForMatches = m_Matches.IsEmpty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * (float)std::min(kMaxVisibleMatches, m_Matches.Size());

    ImGui::BeginChild("ConsoleLog", ImVec2(0.0f, -(reservedForInput + reservedForMatches)), ImGuiChildFlags_Borders);
    for (const LogLine& line : m_Log)
        ImGui::TextColored(line.Color, "%s", line.Text.CStr());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::PushItemWidth(-1.0f);
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion
        | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackEdit;
    bool submitted = ImGui::InputText("##ConsoleInput", m_InputBuffer, sizeof(m_InputBuffer), inputFlags, &ConsolePanel::TextEditCallback, this);
    ImGui::PopItemWidth();

    if (submitted)
        Submit();

    if (m_FocusRequested) {
        ImGui::SetKeyboardFocusHere(-1);
        m_FocusRequested = false;
    }

    if (!m_Matches.IsEmpty()) {
        ImGui::BeginChild("ConsoleMatches", ImVec2(0.0f, reservedForMatches), ImGuiChildFlags_Borders);
        size_t shown = std::min(kMaxVisibleMatches, m_Matches.Size());
        for (size_t i = 0; i < shown; ++i) {
            const String& match = m_Matches[i];
            if (CVar* cvar = CVarRegistry::Find(match))
                ImGui::TextDisabled("%s (%s) = %s -- %s", match.CStr(), cvar->GetDisplayName().CStr(), cvar->ToString().CStr(), cvar->GetDescription().CStr());
            else if (ConsoleCommand* cmd = CVarRegistry::FindCommand(match))
                ImGui::TextDisabled("%s() -- %s", match.CStr(), cmd->GetDescription().CStr());
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

void ConsolePanel::RefreshMatches()
{
    m_Matches.Clear();

    String buffer(m_InputBuffer);
    size_t start = buffer.find_first_not_of(" \t");
    if (start == String::npos)
        return;

    // Only offer name completion for the leading token -- once a space follows it, the user is
    // typing a value/argument and the match list would just be noise.
    size_t sp = buffer.find_first_of(" \t", start);
    if (sp != String::npos)
        return;

    m_Matches = CVarRegistry::Autocomplete(String(buffer.substr(start)));
}

void ConsolePanel::Submit()
{
    String input(m_InputBuffer);
    size_t contentStart = input.find_first_not_of(" \t");

    if (contentStart != String::npos) {
        m_Log.PushBack({ String("] ") + input, ImVec4(0.65f, 0.65f, 0.65f, 1.0f) });

        if (m_CommandHistory.IsEmpty() || m_CommandHistory.back() != input)
            m_CommandHistory.PushBack(input);
        m_HistoryPos = -1;

        TArray<String> outLines;
        CVarRegistry::ExecuteLine(input, outLines);
        for (const String& line : outLines)
            m_Log.PushBack({ line, ImVec4(1.0f, 1.0f, 1.0f, 1.0f) });
    }

    m_InputBuffer[0] = '\0';
    m_Matches.Clear();
    m_FocusRequested = true;
}

int ConsolePanel::TextEditCallback(ImGuiInputTextCallbackData* data)
{
    return static_cast<ConsolePanel*>(data->UserData)->HandleTextEditCallback(data);
}

int ConsolePanel::HandleTextEditCallback(ImGuiInputTextCallbackData* data)
{
    switch (data->EventFlag) {
        case ImGuiInputTextFlags_CallbackCompletion: {
            String buffer(data->Buf);
            if (buffer.find_first_of(" \t") != String::npos)
                break; // only complete the leading command/CVar name

            TArray<String> matches = CVarRegistry::Autocomplete(buffer);
            if (matches.IsEmpty())
                break;

            String completed = matches[0];
            if (matches.Size() > 1) {
                size_t commonLen = completed.Size();
                for (size_t i = 1; i < matches.Size() && commonLen > 0; ++i) {
                    size_t len = 0;
                    while (len < commonLen && len < matches[i].Size() && completed[len] == matches[i][len])
                        len++;
                    commonLen = len;
                }
                completed = String(completed.substr(0, commonLen));
            } else {
                completed.PushBack(' ');
            }

            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, completed.CStr());
            break;
        }
        case ImGuiInputTextFlags_CallbackHistory: {
            int32 prevPos = m_HistoryPos;
            if (data->EventKey == ImGuiKey_UpArrow) {
                if (m_HistoryPos + 1 < (int32)m_CommandHistory.Size())
                    m_HistoryPos++;
            } else if (data->EventKey == ImGuiKey_DownArrow) {
                if (m_HistoryPos >= 0)
                    m_HistoryPos--;
            }

            if (prevPos != m_HistoryPos) {
                String historyValue = m_HistoryPos >= 0 ? m_CommandHistory[m_CommandHistory.Size() - 1 - m_HistoryPos] : String();
                data->DeleteChars(0, data->BufTextLen);
                if (!historyValue.Empty())
                    data->InsertChars(0, historyValue.CStr());
            }
            break;
        }
        case ImGuiInputTextFlags_CallbackEdit: {
            RefreshMatches();
            break;
        }
    }
    return 0;
}
