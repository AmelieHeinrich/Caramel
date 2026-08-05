/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <imgui.h>

// Source/Quake-style drop-down dev console: F1 slides it in from the top of the main viewport.
// Typing live-filters a match list of every registered CVar/ConsoleCommand (Core/CVar.hpp); Tab
// completes, Up/Down cycles command history, Enter executes.
class ConsolePanel
{
public:
    void SetOpen(bool open);
    bool IsOpen() const { return m_TargetOpen; }

    // Call every frame regardless of open state -- it animates itself closed too.
    void Draw(float deltaTime);

private:
    struct LogLine
    {
        String Text;
        ImVec4 Color;
    };

    static int TextEditCallback(ImGuiInputTextCallbackData* data);
    int HandleTextEditCallback(ImGuiInputTextCallbackData* data);

    void Submit();
    void RefreshMatches();

    bool m_TargetOpen = false;
    float m_OpenAmount = 0.0f;
    bool m_FocusRequested = false;

    char m_InputBuffer[256] = {};

    TArray<LogLine> m_Log;

    TArray<String> m_CommandHistory;
    int32 m_HistoryPos = -1;

    TArray<String> m_Matches;
};
