/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "SettingsPanel.hpp"

#include <Caramel/Core/CVar.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdio>

void SettingsPanel::Draw()
{
    ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Reset to Defaults"))
        CVarRegistry::ResetAllToDefaults();
    ImGui::Separator();

    TDictionary<String, TArray<CVar*>> byCategory;
    for (CVar* cvar : CVarRegistry::All())
        byCategory[cvar->GetCategory()].PushBack(cvar);

    TArray<String> categories;
    for (auto& pair : byCategory)
        categories.PushBack(pair.first);
    std::sort(categories.Begin(), categories.End());

    for (const String& category : categories) {
        if (!ImGui::CollapsingHeader(category.CStr(), ImGuiTreeNodeFlags_DefaultOpen))
            continue;

        TArray<CVar*>& cvars = byCategory[category];
        std::sort(cvars.Begin(), cvars.End(), [](CVar* a, CVar* b) { return a->GetName() < b->GetName(); });

        for (CVar* cvar : cvars) {
            // The display name is what the user reads; "##Name" keeps the ImGui ID unique (and
            // stable across sessions) without that dotted console name cluttering the label.
            char label[256];
            std::snprintf(label, sizeof(label), "%s##%s", cvar->GetDisplayName().CStr(), cvar->GetName().CStr());

            switch (cvar->GetType()) {
                case ECVarType::Bool:
                    ImGui::Checkbox(label, cvar->AsBoolPtr());
                    break;
                case ECVarType::Int:
                    if (cvar->GetMaxInt() > cvar->GetMinInt())
                        ImGui::SliderInt(label, cvar->AsIntPtr(), cvar->GetMinInt(), cvar->GetMaxInt());
                    else
                        ImGui::DragInt(label, cvar->AsIntPtr());
                    break;
                case ECVarType::Float:
                    if (cvar->GetMaxFloat() > cvar->GetMinFloat())
                        ImGui::SliderFloat(label, cvar->AsFloatPtr(), cvar->GetMinFloat(), cvar->GetMaxFloat());
                    else
                        ImGui::DragFloat(label, cvar->AsFloatPtr());
                    break;
                case ECVarType::String: {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "%s", cvar->AsStringPtr()->CStr());
                    if (ImGui::InputText(label, buf, sizeof(buf)))
                        *cvar->AsStringPtr() = String(buf);
                    break;
                }
            }
            if (ImGui::IsItemHovered() && !cvar->GetDescription().Empty())
                ImGui::SetTooltip("%s", cvar->GetDescription().CStr());
        }
    }

    ImGui::End();
}
