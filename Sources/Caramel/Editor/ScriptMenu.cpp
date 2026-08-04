/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Editor/ScriptMenu.hpp>
#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Script/ScriptEngine.hpp>
#include <Caramel/Script/ScriptSystem.hpp>

#include <imgui.h>
#include <FontAwesome/FA.h>

void DrawAddScriptMenuItems(EditorContext& context, SceneNode& node, EScriptScope scope, uint32 targetIndex)
{
    if (!context.Scripts)
        return;

    ScriptEngine& engine = context.Scripts->GetEngine();
    TArray<String> paths = engine.GetKnownScriptPaths();

    bool anyOffered = false;

    for (const String& path : paths) {
        const ScriptModuleInfo* moduleInfo = engine.FindModule(path);
        if (!moduleInfo || !moduleInfo->valid || moduleInfo->classes.IsEmpty())
            continue;

        for (const ScriptClassInfo& classInfo : moduleInfo->classes) {
            anyOffered = true;
            if (!ImGui::MenuItem(classInfo.name.CStr()))
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

    if (!anyOffered)
        ImGui::TextDisabled("No compiled scripts in Content/Scripts");
}

void DrawAddScriptMenu(EditorContext& context, SceneNode& node, EScriptScope scope, uint32 targetIndex)
{
    if (!context.Scripts)
        return;

    if (!ImGui::BeginMenu(ICON_FA_SCROLL " Add Script"))
        return;

    DrawAddScriptMenuItems(context, node, scope, targetIndex);
    ImGui::EndMenu();
}
