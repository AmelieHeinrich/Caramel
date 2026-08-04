/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Script/ScriptTypes.hpp>

class SceneNode;
struct EditorContext;

// Menu body listing every compiled script class, attaching the chosen one at the given scope.
// Shared by the Hierarchy context menu and the Inspector's Add Script button.
void DrawAddScriptMenuItems(EditorContext& context, SceneNode& node, EScriptScope scope, uint32 targetIndex);

// The same list wrapped in a BeginMenu, for use inside an existing popup.
void DrawAddScriptMenu(EditorContext& context, SceneNode& node, EScriptScope scope, uint32 targetIndex);
