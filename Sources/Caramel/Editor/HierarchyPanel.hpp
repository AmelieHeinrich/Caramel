/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:03:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

class SceneNode;
class StreamingManager;
struct EditorContext;

class HierarchyPanel
{
public:
    static const char* const kTitle;

    void Draw(EditorContext& context, StreamingManager& streaming);

private:
    void DrawSceneNode(EditorContext& context, StreamingManager& streaming, SceneNode& node);

    SceneNode* m_RenamingNode = nullptr;
    bool m_RenameJustStarted = false;
    char m_RenameBuffer[256] = {};

    // Selection can change from outside the panel (viewport picking), in which case the target row
    // may be inside collapsed parents or scrolled out of view. Track it so the next Draw can expand
    // the ancestors and scroll to it exactly once.
    SceneNode* m_LastSelectedEntity = nullptr;
    uint32 m_LastSelectedInstance = 0;
    void* m_LastSelectedMesh = nullptr;
    bool m_RevealSelection = false;
};
