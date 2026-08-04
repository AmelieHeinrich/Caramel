/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "GpuTimingPanel.hpp"

#include <Caramel/Renderer/Renderer.hpp>
#include <Caramel/Renderer/AccelerationStructureManager.hpp>

#include <imgui.h>

namespace
{
    void DrawTimingSection(const char* label, const TArray<RGPassTiming>& timings)
    {
        ImGui::TextDisabled("%s", label);

        float totalMs = 0.0f;
        for (const RGPassTiming& timing : timings)
            totalMs += timing.gpuTimeMs;
        ImGui::Text("Total: %.3f ms", totalMs);

        float valueColumnX = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("000.000 ms").x;
        for (const RGPassTiming& timing : timings) {
            ImGui::TextUnformatted(timing.name.CStr());
            ImGui::SameLine(valueColumnX);
            ImGui::Text("%.3f ms", timing.gpuTimeMs);
        }
    }
}

void GpuTimingPanel::Draw(Renderer& renderer)
{
    ImGui::SetNextWindowSize(ImVec2(320.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("GPU Timings")) {
        ImGui::End();
        return;
    }

    const TArray<RGPassTiming>& graphicsTimings = renderer.GetLastPassTimings();
    const AccelerationStructureManager& accelStructManager = renderer.GetAccelStructManager();
    const TArray<RGPassTiming>& computeTimings = accelStructManager.GetLastPassTimings();

    if (graphicsTimings.IsEmpty() && computeTimings.IsEmpty()) {
        ImGui::TextDisabled("No timing data yet.");
        ImGui::End();
        return;
    }

    if (!graphicsTimings.IsEmpty())
        DrawTimingSection("Graphics", graphicsTimings);

    if (accelStructManager.IsSupported() && !computeTimings.IsEmpty()) {
        if (!graphicsTimings.IsEmpty())
            ImGui::Separator();
        DrawTimingSection("Compute", computeTimings);
    }

    ImGui::End();
}
