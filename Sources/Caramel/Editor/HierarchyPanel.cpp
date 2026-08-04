/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:03:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "HierarchyPanel.hpp"

#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Application.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Asset/StreamingModel.hpp>

#include <imgui.h>
#include <FontAwesome/FA.h>

#include <cstdio>

namespace
{
    constexpr const char* kSceneNodeDragDropID = "SCENE_NODE_PTR";
}

const char* const HierarchyPanel::kTitle = ICON_FA_SITEMAP " Hierarchy";

void HierarchyPanel::Draw(EditorContext& context, StreamingManager& streaming)
{
    ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin(kTitle);

    if (ImGui::Button(ICON_FA_FOLDER " New Folder"))
        context.CurrentScene.CreateFolder(nullptr, "New Folder");

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_EXPORT " Save Scene...")) {
        static SDL_DialogFileFilter filters[] = { { "Caramel Scene", "cscene" } };
        SDL_ShowSaveFileDialog(&Application::OnSaveDialogResult, context.Owner, context.Window, filters, 1, "Content/Scenes/");
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_IMPORT " Load Scene...")) {
        static SDL_DialogFileFilter filters[] = { { "Caramel Scene", "cscene" } };
        SDL_ShowOpenFileDialog(&Application::OnOpenDialogResult, context.Owner, context.Window, filters, 1, "Content/Scenes/", false);
    }

    ImGui::Separator();
    DrawSceneNode(context, streaming, context.CurrentScene.GetRoot());

    ImGui::Separator();
    ImGui::TextDisabled(ICON_FA_DOWNLOAD " Drop here to move to root");
    ImGui::Dummy(ImVec2(-1.0f, 24.0f));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(kSceneNodeDragDropID)) {
            SceneNode* dragged = *(SceneNode**)accepted->Data;
            context.CurrentScene.Reparent(dragged, nullptr);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}

void HierarchyPanel::DrawSceneNode(EditorContext& context, StreamingManager& streaming, SceneNode& node)
{
    SceneNode* pendingDelete = nullptr;

    for (size_t i = 0; i < node.children.Size(); ++i) {
        SceneNode& child = *node.children[i];
        ImGui::PushID(&child);

        bool isFolder = (child.type == ESceneNodeType::Folder);
        bool renaming = (m_RenamingNode == &child);

        char nodeLabel[300];
        std::snprintf(nodeLabel, sizeof(nodeLabel), "%s %s", isFolder ? ICON_FA_FOLDER : ICON_FA_CUBES, child.name.CStr());

        bool open = ImGui::TreeNodeEx(renaming ? "##renaming" : nodeLabel,
                                       ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

        if (!renaming && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_RenamingNode = &child;
            m_RenameJustStarted = true;
            std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", child.name.CStr());
        }

        if (!renaming && ImGui::BeginDragDropSource()) {
            SceneNode* payload = &child;
            ImGui::SetDragDropPayload(kSceneNodeDragDropID, &payload, sizeof(SceneNode*));
            ImGui::TextUnformatted(child.name.CStr());
            ImGui::EndDragDropSource();
        }

        if (isFolder && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(kSceneNodeDragDropID)) {
                SceneNode* dragged = *(SceneNode**)accepted->Data;
                context.CurrentScene.Reparent(dragged, &child);
            }
            ImGui::EndDragDropTarget();
        }

        if (!renaming && ImGui::BeginPopupContextItem()) {
            if (isFolder && ImGui::MenuItem(ICON_FA_FOLDER " New Folder"))
                context.CurrentScene.CreateFolder(&child, "New Folder");
            if (!isFolder && ImGui::MenuItem(ICON_FA_PLUS " Add Instance"))
                context.CurrentScene.AddInstance(&child, Instance{});
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete"))
                pendingDelete = &child;
            ImGui::EndPopup();
        }

        if (renaming) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (m_RenameJustStarted) {
                ImGui::SetKeyboardFocusHere();
                m_RenameJustStarted = false;
            }
            bool committed = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (committed) {
                child.name = m_RenameBuffer;
                m_RenamingNode = nullptr;
            } else if (ImGui::IsItemDeactivated()) {
                m_RenamingNode = nullptr;
            }
        }

        if (open) {
            if (isFolder) {
                DrawSceneNode(context, streaming, child);
            } else {
                const TArray<TShared<StreamingModel>>& models = streaming.GetModels();
                for (uint32 j = 0; j < (uint32)child.instances.Size(); ++j) {
                    ImGui::PushID((int)j);

                    char instLabel[300];
                    std::snprintf(instLabel, sizeof(instLabel), "%s %s", ICON_FA_LOCATION_ARROW, child.instances[j].name.CStr());

                    ImGuiTreeNodeFlags instFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (context.SelectedEntity == &child && context.SelectedInstance == j && !context.SelectedMesh)
                        instFlags |= ImGuiTreeNodeFlags_Selected;

                    bool instanceOpen = ImGui::TreeNodeEx(instLabel, instFlags);
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        context.SelectedEntity = &child;
                        context.SelectedInstance = j;
                        context.SelectedMesh = nullptr;
                    }

                    if (instanceOpen) {
                        for (uint32 meshIndex : child.meshIndices) {
                            StreamingModel* mesh = models[meshIndex].get();

                            char meshLabel[300];
                            std::snprintf(meshLabel, sizeof(meshLabel), "%s %s", ICON_FA_CUBE, mesh->GetMesh().name.CStr());

                            ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                                          | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanAvailWidth;
                            if (context.SelectedEntity == &child && context.SelectedInstance == j && context.SelectedMesh == mesh)
                                meshFlags |= ImGuiTreeNodeFlags_Selected;

                            ImGui::PushID((int)meshIndex);
                            ImGui::TreeNodeEx(meshLabel, meshFlags);
                            if (ImGui::IsItemClicked()) {
                                context.SelectedEntity = &child;
                                context.SelectedInstance = j;
                                context.SelectedMesh = mesh;
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (pendingDelete) {
        if (context.SelectedEntity == pendingDelete) {
            context.SelectedEntity = nullptr;
            context.SelectedMesh = nullptr;
        }
        if (m_RenamingNode == pendingDelete)
            m_RenamingNode = nullptr;
        context.CurrentScene.DeleteNode(pendingDelete);
    }
}
