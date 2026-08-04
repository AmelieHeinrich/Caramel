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
};
