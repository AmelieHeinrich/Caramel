/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "RenderGraphPanel.hpp"

#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Editor/EditorTheme.hpp>
#include <Caramel/Renderer/Renderer.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphDebugInfo.hpp>

#include <imgui.h>

#include <string>

namespace
{
    const char* QueueLabel(RGQueue queue)
    {
        switch (queue) {
            case RGQueue::Graphics: return "GFX";
            case RGQueue::Compute:  return "CMP";
            case RGQueue::Transfer: return "XFR";
        }
        return "?";
    }

    ImU32 QueueColor(RGQueue queue)
    {
        switch (queue) {
            case RGQueue::Graphics: return IM_COL32(90, 140, 220, 255);
            case RGQueue::Compute:  return IM_COL32(220, 150, 60, 255);
            case RGQueue::Transfer: return IM_COL32(150, 90, 200, 255);
        }
        return IM_COL32(150, 150, 150, 255);
    }

    // Resources sharing a heap region get the same color, so aliasing reads at a glance -- imported
    // (externally owned) and committed (heapAliasGroup == -1) resources get a neutral gray instead.
    ImU32 AliasGroupColor(int32 group)
    {
        if (group < 0)
            return IM_COL32(120, 120, 128, 255);
        static const ImU32 kPalette[] = {
            IM_COL32(230, 126, 34, 255), IM_COL32(46, 204, 113, 255), IM_COL32(52, 152, 219, 255),
            IM_COL32(155, 89, 182, 255), IM_COL32(241, 196, 15, 255), IM_COL32(26, 188, 156, 255),
        };
        return kPalette[(uint32)group % (sizeof(kPalette) / sizeof(kPalette[0]))];
    }

    const char* ResourceStateName(agfx::ResourceState state)
    {
        switch (state) {
            case agfx::ResourceState::Common: return "Common";
            case agfx::ResourceState::VertexAndConstantBuffer: return "VertexAndConstantBuffer";
            case agfx::ResourceState::IndexBuffer: return "IndexBuffer";
            case agfx::ResourceState::RenderTarget: return "RenderTarget";
            case agfx::ResourceState::UnorderedAccess: return "UnorderedAccess";
            case agfx::ResourceState::DepthWrite: return "DepthWrite";
            case agfx::ResourceState::DepthRead: return "DepthRead";
            case agfx::ResourceState::NonPixelShaderResource: return "NonPixelShaderResource";
            case agfx::ResourceState::PixelShaderResource: return "PixelShaderResource";
            case agfx::ResourceState::IndirectArgument: return "IndirectArgument";
            case agfx::ResourceState::CopyDest: return "CopyDest";
            case agfx::ResourceState::CopySource: return "CopySource";
            case agfx::ResourceState::RaytracingAccelerationStructure: return "RaytracingAccelerationStructure";
            case agfx::ResourceState::GenericRead: return "GenericRead";
            case agfx::ResourceState::AllShaderResource: return "AllShaderResource";
            case agfx::ResourceState::Present: return "Present";
        }
        return "Unknown";
    }
}

void RenderGraphPanel::Draw(EditorContext& context, Renderer& renderer)
{
    (void)context;

    ImGui::SetNextWindowSize(ImVec2(760.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Resource Dependency Viewer")) {
        ImGui::End();
        return;
    }

    const RenderGraphDebugInfo& info = renderer.GetLastGraphDebugInfo();
    if (info.passes.IsEmpty()) {
        ImGui::TextDisabled("No graph recorded yet.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Last frame's compiled graph -- %d passes, %d resources", (int)info.passes.Size(), (int)info.resources.Size());
    ImGui::Separator();

    const float columnWidth = EditorTheme::Em(9.0f);
    const float rowHeight = EditorTheme::Em(1.5f);
    const float headerHeight = EditorTheme::Em(2.8f);
    const float labelWidth = EditorTheme::Em(11.0f);

    // Culled/never-used resources have no meaningful [firstUsePass, lastUsePass] to draw a bar for --
    // they're rolled into the summary line below instead.
    TArray<uint32> visibleResources;
    for (uint32 i = 0; i < info.resources.Size(); ++i) {
        if (info.resources[i].firstUsePass != UINT32_MAX)
            visibleResources.PushBack(i);
    }

    float contentWidth = labelWidth + columnWidth * info.passes.Size();
    float contentHeight = headerHeight + rowHeight * visibleResources.Size() + EditorTheme::Em(1.5f);

    ImGui::BeginChild("RenderGraphCanvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::Dummy(ImVec2(contentWidth, contentHeight));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetItemRectMin();

    for (uint32 p = 0; p < info.passes.Size(); ++p) {
        const RGDebugPassInfo& pass = info.passes[p];

        ImVec2 topLeft(origin.x + labelWidth + columnWidth * p, origin.y);
        ImVec2 bottomRight(topLeft.x + columnWidth - EditorTheme::Em(0.3f), topLeft.y + headerHeight - EditorTheme::Em(0.3f));

        ImU32 fill = pass.wasCulled ? IM_COL32(45, 45, 48, 255) : IM_COL32(60, 64, 74, 255);
        drawList->AddRectFilled(topLeft, bottomRight, fill, 4.0f);
        drawList->AddRect(topLeft, bottomRight, QueueColor(pass.queue), 4.0f, 0, 1.5f);

        ImVec2 textPos(topLeft.x + EditorTheme::Em(0.4f), topLeft.y + EditorTheme::Em(0.3f));
        ImU32 textColor = pass.wasCulled ? IM_COL32(140, 140, 140, 255) : IM_COL32(235, 235, 235, 255);
        drawList->AddText(textPos, textColor, pass.name.CStr());

        String badge = QueueLabel(pass.queue);
        if (pass.wasCulled)
            badge += " (culled)";
        drawList->AddText(ImVec2(textPos.x, textPos.y + ImGui::GetFontSize() + 2.0f), IM_COL32(180, 180, 180, 255), badge.CStr());

        if (ImGui::IsMouseHoveringRect(topLeft, bottomRight) && ImGui::BeginTooltip()) {
            ImGui::Text("%s", pass.name.CStr());
            ImGui::Text("Queue: %s", QueueLabel(pass.queue));
            ImGui::Text("Kind: %s", pass.isAttachmentPass ? "Attachment pass" : "Generic pass");
            if (pass.wasCulled)
                ImGui::TextColored(ImVec4(0.9f, 0.55f, 0.3f, 1.0f), "Culled: no reachable consumer this frame");

            if (!pass.colorAttachmentResourceIndices.IsEmpty() || pass.depthAttachmentResourceIndex >= 0) {
                ImGui::Separator();
                ImGui::TextDisabled("Attachments");
                for (uint32 idx : pass.colorAttachmentResourceIndices)
                    ImGui::BulletText("Color: %s", info.resources[idx].name.CStr());
                if (pass.depthAttachmentResourceIndex >= 0)
                    ImGui::BulletText("Depth: %s", info.resources[(uint32)pass.depthAttachmentResourceIndex].name.CStr());
            }
            if (!pass.readResourceIndices.IsEmpty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Reads");
                for (uint32 r = 0; r < pass.readResourceIndices.Size(); ++r)
                    ImGui::BulletText("%s (%s)", info.resources[pass.readResourceIndices[r]].name.CStr(), ResourceStateName(pass.readStates[r]));
            }
            if (!pass.writeResourceIndices.IsEmpty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Writes");
                for (uint32 r = 0; r < pass.writeResourceIndices.Size(); ++r)
                    ImGui::BulletText("%s (%s)", info.resources[pass.writeResourceIndices[r]].name.CStr(), ResourceStateName(pass.writeStates[r]));
            }
            ImGui::EndTooltip();
        }
    }

    for (uint32 row = 0; row < visibleResources.Size(); ++row) {
        const RGDebugResourceInfo& res = info.resources[visibleResources[row]];

        ImVec2 labelPos(origin.x, origin.y + headerHeight + rowHeight * row + EditorTheme::Em(0.15f));
        drawList->AddText(labelPos, IM_COL32(220, 220, 220, 255), res.name.CStr());

        float barY0 = origin.y + headerHeight + rowHeight * row + EditorTheme::Em(0.2f);
        float barY1 = barY0 + rowHeight - EditorTheme::Em(0.4f);
        float barX0 = origin.x + labelWidth + columnWidth * res.firstUsePass + EditorTheme::Em(0.2f);
        float barX1 = origin.x + labelWidth + columnWidth * (res.lastUsePass + 1) - EditorTheme::Em(0.2f);

        ImU32 color = res.isImported ? IM_COL32(120, 130, 200, 220) : AliasGroupColor(res.heapAliasGroup);
        drawList->AddRectFilled(ImVec2(barX0, barY0), ImVec2(barX1, barY1), color, 3.0f);

        if (ImGui::IsMouseHoveringRect(ImVec2(barX0, barY0), ImVec2(barX1, barY1)) && ImGui::BeginTooltip()) {
            ImGui::Text("%s", res.name.CStr());
            ImGui::Text("Type: %s", res.type == RGResourceType::Texture ? "Texture" : "Buffer");
            ImGui::Text("Origin: %s", res.isImported ? "Imported (externally owned)" : "Transient (graph-owned)");
            ImGui::Text("Lifetime: pass %u - %u", res.firstUsePass, res.lastUsePass);
            if (res.sizeBytes > 0)
                ImGui::Text("Size: %.2f KB", (double)res.sizeBytes / 1024.0);
            if (res.heapAliasGroup >= 0)
                ImGui::Text("Heap alias group: #%d", res.heapAliasGroup);
            ImGui::EndTooltip();
        }
    }

    ImGui::EndChild();

    uint32 unusedCount = (uint32)info.resources.Size() - (uint32)visibleResources.Size();
    if (unusedCount > 0)
        ImGui::TextDisabled("%u resource(s) unused this frame (never read or written by a kept pass).", unusedCount);

    ImGui::End();
}
