/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "CpuProfilerPanel.hpp"

#include <Caramel/Core/CpuProfiler.hpp>
#include <Caramel/Editor/EditorTheme.hpp>

#include <imgui.h>
#include <FontAwesome/FA.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace
{
    // FNV-1a over the zone name -- the same name always gets the same color, so one zone reads as
    // one consistent hue everywhere it appears (across threads, across frames, nested or not).
    ImU32 ZoneColor(const char* name)
    {
        uint32 hash = 2166136261u;
        for (const char* c = name; *c; ++c)
            hash = (hash ^ (uint32)(unsigned char)*c) * 16777619u;

        static const ImU32 kPalette[] = {
            IM_COL32(90, 140, 220, 255), IM_COL32(220, 150, 60, 255), IM_COL32(150, 90, 200, 255),
            IM_COL32(80, 200, 120, 255), IM_COL32(220, 90, 110, 255), IM_COL32(90, 200, 200, 255),
            IM_COL32(200, 200, 90, 255), IM_COL32(160, 140, 230, 255), IM_COL32(230, 126, 34, 255),
            IM_COL32(46, 204, 113, 255),
        };
        return kPalette[hash % (sizeof(kPalette) / sizeof(kPalette[0]))];
    }

    uint16 MaxDepth(const TArray<CpuProfileEvent>& events)
    {
        uint16 maxDepth = 0;
        for (const CpuProfileEvent& evt : events)
            maxDepth = std::max(maxDepth, evt.depth);
        return maxDepth;
    }

    // Self time excludes children -- without it an outer zone (e.g. "Frame") would trivially
    // dominate the totals list, hiding what is actually expensive underneath it. Events are stored
    // in push order (a DFS preorder), so a node's direct children are exactly the run of following
    // events one level deeper, up to the next event at its own depth or shallower.
    void ComputeSelfTimes(const TArray<CpuProfileEvent>& events, TArray<float>& outSelfMs)
    {
        outSelfMs.Resize(events.Size());
        for (uint32 i = 0; i < events.Size(); ++i) {
            float childSum = 0.0f;
            uint16 depth = events[i].depth;
            for (uint32 j = i + 1; j < events.Size() && events[j].depth > depth; ++j) {
                if (events[j].depth == depth + 1)
                    childSum += (events[j].endMs - events[j].startMs);
            }
            outSelfMs[i] = (events[i].endMs - events[i].startMs) - childSum;
        }
    }

    void DrawTotalsTable(const TArray<CpuProfileEvent>& events)
    {
        TArray<float> selfMs;
        ComputeSelfTimes(events, selfMs);

        TDictionary<String, float> totalByName;
        TDictionary<String, uint32> countByName;
        TDictionary<String, bool> isWaitByName;
        for (uint32 i = 0; i < events.Size(); ++i) {
            String name = events[i].name;
            totalByName[name] += selfMs[i];
            countByName[name] += 1u;
            isWaitByName[name] = events[i].isWait;
        }

        TArray<std::pair<String, float>> rows;
        for (const auto& kv : totalByName)
            rows.PushBack({ kv.first, kv.second });
        std::sort(rows.Begin(), rows.End(), [](const auto& a, const auto& b) { return a.second > b.second; });

        if (!ImGui::BeginTable("Totals", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
            return;
        ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, EditorTheme::Em(5.5f));
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, EditorTheme::Em(4.0f));
        ImGui::TableHeadersRow();

        for (const auto& row : rows) {
            bool isWait = isWaitByName[row.first];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (isWait)
                ImGui::TextDisabled("%s (wait)", row.first.CStr());
            else
                ImGui::TextUnformatted(row.first.CStr());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f ms", row.second);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", countByName[row.first]);
        }
        ImGui::EndTable();
    }
}

void CpuProfilerPanel::Draw()
{
    ImGui::SetNextWindowSize(ImVec2(780.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_CHART_GANTT " CPU Profiler")) {
        ImGui::End();
        return;
    }

    if (m_Paused && m_DisplaySnapshot.IsEmpty())
        m_Paused = false;

    if (!m_Paused)
        m_DisplaySnapshot = CpuProfiler::GetLastFrameSnapshot();
    const TArray<CpuProfileThreadSnapshot>& threads = m_DisplaySnapshot;

    if (threads.IsEmpty()) {
        ImGui::TextDisabled("No profiling data yet.");
        ImGui::End();
        return;
    }

    float frameMs = 0.0f;
    for (const CpuProfileThreadSnapshot& thread : threads)
        for (const CpuProfileEvent& evt : thread.events)
            frameMs = std::max(frameMs, evt.endMs);

    if (ImGui::Button(m_Paused ? ICON_FA_PLAY " Resume" : ICON_FA_PAUSE " Pause"))
        m_Paused = !m_Paused;
    ImGui::SameLine();
    ImGui::Text("Last frame: %.3f ms across %d thread(s)", frameMs, (int)threads.Size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(EditorTheme::Em(8.0f));
    ImGui::SliderFloat("Zoom", &m_PixelsPerMs, 10.0f, 800.0f, "%.0f px/ms");
    ImGui::Separator();

    const float labelWidth = EditorTheme::Em(9.0f);
    const float rowHeight = EditorTheme::Em(1.4f);
    const float laneHeaderHeight = EditorTheme::Em(1.4f);
    const float laneSpacing = EditorTheme::Em(0.6f);

    float contentWidth = labelWidth + frameMs * m_PixelsPerMs + EditorTheme::Em(2.0f);
    float contentHeight = 0.0f;
    for (const CpuProfileThreadSnapshot& thread : threads)
        contentHeight += laneHeaderHeight + (MaxDepth(thread.events) + 1) * rowHeight + laneSpacing;

    ImGui::BeginChild("CpuProfilerCanvas", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.6f),
                       ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::Dummy(ImVec2(contentWidth, contentHeight));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetItemRectMin();

    float laneY = origin.y;
    for (const CpuProfileThreadSnapshot& thread : threads) {
        uint16 maxDepth = MaxDepth(thread.events);
        float laneHeight = laneHeaderHeight + (maxDepth + 1) * rowHeight;

        // Waits at different depths can't just be summed (a wait nested inside another would double
        // count), but nothing in this codebase nests one wait inside another, so a flat sum is exact
        // in practice and far simpler than tracking overlap.
        float waitMs = 0.0f;
        for (const CpuProfileEvent& evt : thread.events)
            if (evt.isWait)
                waitMs += (evt.endMs - evt.startMs);

        if (waitMs > 0.0f) {
            char header[128];
            snprintf(header, sizeof(header), "%s (wait: %.2f ms)", thread.name.CStr(), waitMs);
            drawList->AddText(ImVec2(origin.x, laneY), IM_COL32(220, 220, 220, 255), header);
        } else {
            drawList->AddText(ImVec2(origin.x, laneY), IM_COL32(220, 220, 220, 255), thread.name.CStr());
        }
        drawList->AddLine(ImVec2(origin.x, laneY + laneHeaderHeight - 2.0f),
                           ImVec2(origin.x + contentWidth, laneY + laneHeaderHeight - 2.0f), IM_COL32(70, 70, 76, 255));

        for (const CpuProfileEvent& evt : thread.events) {
            float x0 = origin.x + labelWidth + evt.startMs * m_PixelsPerMs;
            float x1 = origin.x + labelWidth + evt.endMs * m_PixelsPerMs;
            if (x1 - x0 < 1.0f)
                x1 = x0 + 1.0f;
            float y0 = laneY + laneHeaderHeight + evt.depth * rowHeight;
            float y1 = y0 + rowHeight - EditorTheme::Em(0.15f);

            // Wait zones are CPU idle time (a fence, a GPU readback), not compute -- a flat grey with
            // diagonal hatching instead of the per-name hash color so they read as "stalled", not as
            // just another expensive zone, no matter how wide the bar is.
            if (evt.isWait) {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(80, 82, 88, 255), 2.0f);
                drawList->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
                float stripeSpacing = EditorTheme::Em(0.5f);
                for (float sx = x0 - (y1 - y0); sx < x1; sx += stripeSpacing)
                    drawList->AddLine(ImVec2(sx, y1), ImVec2(sx + (y1 - y0), y0), IM_COL32(120, 122, 128, 255), 1.5f);
                drawList->PopClipRect();
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(140, 142, 148, 255), 2.0f);
            } else {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), ZoneColor(evt.name), 2.0f);
            }

            if (x1 - x0 > EditorTheme::Em(2.0f)) {
                drawList->PushClipRect(ImVec2(x0 + 2.0f, y0), ImVec2(x1 - 2.0f, y1), true);
                drawList->AddText(ImVec2(x0 + 3.0f, y0 + 1.0f), IM_COL32(15, 15, 18, 255), evt.name);
                drawList->PopClipRect();
            }

            if (ImGui::IsMouseHoveringRect(ImVec2(x0, y0), ImVec2(x1, y1)) && ImGui::BeginTooltip()) {
                ImGui::Text("%s", evt.name);
                if (evt.isWait)
                    ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.8f, 1.0f), "Blocking wait -- CPU idle, not compute");
                ImGui::Text("Thread: %s", thread.name.CStr());
                ImGui::Text("Duration: %.4f ms", evt.endMs - evt.startMs);
                ImGui::Text("Start: %.4f ms", evt.startMs);
                ImGui::EndTooltip();
            }
        }

        laneY += laneHeight + laneSpacing;
    }

    ImGui::EndChild();

    if (ImGui::CollapsingHeader("Totals (self time)", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const CpuProfileThreadSnapshot& thread : threads) {
            ImGui::PushID(thread.name.CStr());
            ImGui::TextDisabled("%s", thread.name.CStr());
            DrawTotalsTable(thread.events);
            ImGui::Spacing();
            ImGui::PopID();
        }
    }

    ImGui::End();
}
