/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:04:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

class StreamingManager;
struct EditorContext;

class InspectorPanel
{
public:
    static const char* const kTitle;

    void Draw(EditorContext& context, StreamingManager& streaming);

private:
    void DrawScriptSection(EditorContext& context);
};
