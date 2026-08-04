/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:03:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "HierarchyPanel.hpp"

#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Editor/EditorTheme.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Asset/StreamingModel.hpp>
#include <Caramel/Script/ScriptEngine.hpp>
#include <Caramel/Script/ScriptSystem.hpp>
#include <Caramel/Editor/ScriptMenu.hpp>

#include <imgui.h>
#include <FontAwesome/FA.h>

#include <cstdio>

namespace
{
    constexpr const char* kSceneNodeDragDropID = "SCENE_NODE_PTR";

    const char* NodeIcon(ESceneNodeType type)
    {
        switch (type) {
            case ESceneNodeType::Folder: return ICON_FA_FOLDER;
            case ESceneNodeType::Empty:  return ICON_FA_CIRCLE_NOTCH;
            default:                     return ICON_FA_CUBES;
        }
    }

    bool SubtreeContains(const SceneNode& node, const SceneNode* target)
    {
        if (&node == target)
            return true;

        for (const TUnique<SceneNode>& child : node.children) {
            if (SubtreeContains(*child, target))
                return true;
        }
        return false;
    }
}

const char* const HierarchyPanel::kTitle = ICON_FA_SITEMAP " Hierarchy";

void HierarchyPanel::Draw(EditorContext& context, StreamingManager& streaming)
{
    ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin(kTitle);

    m_RevealSelection = context.SelectedEntity != nullptr
        && (context.SelectedEntity != m_LastSelectedEntity
            || context.SelectedInstance != m_LastSelectedInstance
            || (void*)context.SelectedMesh != m_LastSelectedMesh);

    m_LastSelectedEntity = context.SelectedEntity;
    m_LastSelectedInstance = context.SelectedInstance;
    m_LastSelectedMesh = (void*)context.SelectedMesh;

    // Only the two create actions live here -- this panel docks to 20% of the window width, and a
    // row of labelled buttons overflowed it. Scene I/O and script reload are on the main menu bar.
    if (EditorTheme::IconButton(ICON_FA_FOLDER, "New Folder"))
        context.CurrentScene.CreateFolder(nullptr, "New Folder");

    ImGui::SameLine();
    if (EditorTheme::IconButton(ICON_FA_CIRCLE_NOTCH, "New Empty"))
        context.CurrentScene.CreateEmptyEntity(nullptr, "New Empty");

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
        bool hasInstances = SceneNodeTypeHasInstances(child.type);
        bool canParent = SceneNodeTypeCanParent(child.type);
        bool renaming = (m_RenamingNode == &child);

        char nodeLabel[300];
        std::snprintf(nodeLabel, sizeof(nodeLabel), "%s %s", NodeIcon(child.type), child.name.CStr());

        bool onPathToSelection = m_RevealSelection && SubtreeContains(child, context.SelectedEntity);
        if (onPathToSelection)
            ImGui::SetNextItemOpen(true);

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

        if (canParent && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(kSceneNodeDragDropID)) {
                SceneNode* dragged = *(SceneNode**)accepted->Data;
                context.CurrentScene.Reparent(dragged, &child);
            }
            ImGui::EndDragDropTarget();
        }

        if (!renaming && ImGui::BeginPopupContextItem()) {
            if (canParent && ImGui::MenuItem(ICON_FA_FOLDER " New Folder"))
                context.CurrentScene.CreateFolder(&child, "New Folder");
            if (canParent && ImGui::MenuItem(ICON_FA_CIRCLE_NOTCH " New Empty"))
                context.CurrentScene.CreateEmptyEntity(&child, "New Empty");
            if (hasInstances && ImGui::MenuItem(ICON_FA_PLUS " Add Instance"))
                context.CurrentScene.AddInstance(&child, Instance{});
            DrawAddScriptMenu(context, child, EScriptScope::Node, 0);
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
            if (hasInstances) {
                const TArray<TShared<StreamingModel>>& models = streaming.GetModels();
                for (uint32 j = 0; j < (uint32)child.instances.Size(); ++j) {
                    ImGui::PushID((int)j);

                    char instLabel[300];
                    std::snprintf(instLabel, sizeof(instLabel), "%s %s", ICON_FA_LOCATION_ARROW, child.instances[j].name.CStr());

                    bool isSelectedInstance = context.SelectedEntity == &child && context.SelectedInstance == j;

                    ImGuiTreeNodeFlags instFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (isSelectedInstance && !context.SelectedMesh)
                        instFlags |= ImGuiTreeNodeFlags_Selected;

                    // Expand so a picked mesh under this instance is visible too.
                    if (m_RevealSelection && isSelectedInstance && context.SelectedMesh)
                        ImGui::SetNextItemOpen(true);

                    bool instanceOpen = ImGui::TreeNodeEx(instLabel, instFlags);

                    if (m_RevealSelection && isSelectedInstance && !context.SelectedMesh)
                        ImGui::SetScrollHereY(0.5f);

                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        context.SelectedEntity = &child;
                        context.SelectedInstance = j;
                        context.SelectedMesh = nullptr;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        DrawAddScriptMenu(context, child, EScriptScope::Instance, j);
                        ImGui::EndPopup();
                    }

                    if (instanceOpen) {
                        for (uint32 slot = 0; slot < (uint32)child.meshIndices.Size(); ++slot) {
                            uint32 meshIndex = child.meshIndices[slot];
                            StreamingModel* mesh = models[meshIndex].get();

                            char meshLabel[300];
                            std::snprintf(meshLabel, sizeof(meshLabel), "%s %s", ICON_FA_CUBE, mesh->GetMesh().name.CStr());

                            ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                                          | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanAvailWidth;
                            if (context.SelectedEntity == &child && context.SelectedInstance == j && context.SelectedMesh == mesh)
                                meshFlags |= ImGuiTreeNodeFlags_Selected;

                            ImGui::PushID((int)meshIndex);
                            ImGui::TreeNodeEx(meshLabel, meshFlags);

                            if (m_RevealSelection && isSelectedInstance && context.SelectedMesh == mesh)
                                ImGui::SetScrollHereY(0.5f);

                            if (ImGui::IsItemClicked()) {
                                context.SelectedEntity = &child;
                                context.SelectedInstance = j;
                                context.SelectedMesh = mesh;
                            }

                            if (ImGui::BeginPopupContextItem()) {
                                DrawAddScriptMenu(context, child, EScriptScope::Mesh, slot);
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }
            }

            if (!child.children.IsEmpty())
                DrawSceneNode(context, streaming, child);

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
