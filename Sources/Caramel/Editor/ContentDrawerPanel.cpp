/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ContentDrawerPanel.hpp"

#include <imgui.h>
#include <FontAwesome/FA.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace
{
    constexpr const char* kContentFileDragDropID = "CONTENT_FILE_PATH";
}

void ContentDrawerPanel::Draw()
{
    constexpr float kBarHeight = 30.0f;
    constexpr float kDrawerHeight = 320.0f;
    constexpr float kAnimDuration = 0.35f;
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    m_AnimTime += ImGui::GetIO().DeltaTime;
    float t = std::min(m_AnimTime / kAnimDuration, 1.0f);
    float tm1 = t - 1.0f;
    float eased = 1.0f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
    float progress = std::max(m_Open ? eased : 1.0f - eased, 0.0f);
    float drawerHeight = kDrawerHeight * progress;

    ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                               | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - kBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, kBarHeight));
    ImGui::Begin("##ContentDrawerBar", nullptr, barFlags);
    if (ImGui::Button(m_Open ? ICON_FA_FOLDER_OPEN " Content" : ICON_FA_FOLDER " Content")) {
        m_Open = !m_Open;
        m_AnimTime = 0.0f;
    }
    ImGui::End();

    if (drawerHeight <= 0.0f)
        return;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - kBarHeight - drawerHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, drawerHeight));
    ImGui::Begin("##ContentDrawer", nullptr, barFlags & ~ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("Drag a mesh file onto the viewport to add it to the scene");
    ImGui::Separator();
    DrawContentDirectory("Content");
    ImGui::End();
}

void ContentDrawerPanel::DrawContentDirectory(const String& directory)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(directory.CStr(), ec))
        return;

    TArray<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(directory.CStr(), ec))
        entries.PushBack(entry);

    std::sort(entries.Begin(), entries.End(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        if (a.is_directory() != b.is_directory())
            return a.is_directory() > b.is_directory();
        return a.path().filename().string() < b.path().filename().string();
    });

    for (const fs::directory_entry& entry : entries) {
        const fs::path& p = entry.path();
        String label = String(p.filename().string());

        ImGui::PushID(label.CStr());

        char entryLabel[300];
        std::snprintf(entryLabel, sizeof(entryLabel), "%s %s", entry.is_directory() ? ICON_FA_FOLDER : ICON_FA_CUBE, label.CStr());

        if (entry.is_directory()) {
            if (ImGui::TreeNodeEx(entryLabel, ImGuiTreeNodeFlags_SpanAvailWidth)) {
                DrawContentDirectory(String(p.string()));
                ImGui::TreePop();
            }
        } else {
            std::string ext = p.extension().string();
            if (ext == ".gltf" || ext == ".glb" || ext == ".cmdl") {
                ImGui::TreeNodeEx(entryLabel, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                             | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanAvailWidth);
                if (ImGui::BeginDragDropSource()) {
                    std::string pathStr = p.string();
                    ImGui::SetDragDropPayload(kContentFileDragDropID, pathStr.c_str(), pathStr.size() + 1);
                    ImGui::TextUnformatted(pathStr.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }

        ImGui::PopID();
    }
}
