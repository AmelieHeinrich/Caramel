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
#include <Caramel/Script/ScriptEngine.hpp>
#include <Caramel/Script/ScriptSystem.hpp>

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

    // Offers every class in every compiled module, then attaches it at the scope of the row the
    // menu was opened on.
    void DrawAddScriptMenu(EditorContext& context, SceneNode& node, EScriptScope scope, uint32 targetIndex)
    {
        if (!context.Scripts)
            return;

        if (!ImGui::BeginMenu(ICON_FA_SCROLL " Add Script"))
            return;

        ScriptEngine& engine = context.Scripts->GetEngine();
        TArray<String> paths = engine.GetKnownScriptPaths();

        if (paths.IsEmpty())
            ImGui::TextDisabled("No scripts in Content/Scripts");

        for (const String& path : paths) {
            const ScriptModuleInfo* moduleInfo = engine.FindModule(path);
            if (!moduleInfo || !moduleInfo->valid || moduleInfo->classes.IsEmpty())
                continue;

            for (const ScriptClassInfo& classInfo : moduleInfo->classes) {
                char label[320];
                std::snprintf(label, sizeof(label), "%s", classInfo.name.CStr());

                if (!ImGui::MenuItem(label))
                    continue;

                ScriptComponent component;
                component.scriptPath = path;
                component.className = classInfo.name;
                component.scope = scope;
                component.targetIndex = targetIndex;
                node.scripts.PushBack(component);

                context.Scripts->OnComponentAdded(node, (uint32)node.scripts.Size() - 1);
            }
        }

        ImGui::EndMenu();
    }
}

const char* const HierarchyPanel::kTitle = ICON_FA_SITEMAP " Hierarchy";

void HierarchyPanel::Draw(EditorContext& context, StreamingManager& streaming)
{
    ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin(kTitle);

    if (ImGui::Button(ICON_FA_FOLDER " New Folder"))
        context.CurrentScene.CreateFolder(nullptr, "New Folder");

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CIRCLE_NOTCH " New Empty"))
        context.CurrentScene.CreateEmptyEntity(nullptr, "New Empty");

    ImGui::SameLine();
    if (context.Scripts && ImGui::Button(ICON_FA_ROTATE " Reload Scripts"))
        context.Scripts->ReloadAll();

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
        bool hasInstances = SceneNodeTypeHasInstances(child.type);
        bool canParent = SceneNodeTypeCanParent(child.type);
        bool renaming = (m_RenamingNode == &child);

        char nodeLabel[300];
        std::snprintf(nodeLabel, sizeof(nodeLabel), "%s %s", NodeIcon(child.type), child.name.CStr());

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

                    ImGuiTreeNodeFlags instFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (context.SelectedEntity == &child && context.SelectedInstance == j && !context.SelectedMesh)
                        instFlags |= ImGuiTreeNodeFlags_Selected;

                    bool instanceOpen = ImGui::TreeNodeEx(instLabel, instFlags);
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
