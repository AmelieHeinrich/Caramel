/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:04:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

class StreamingManager;
struct EditorContext;
struct ModelMaterial;

class InspectorPanel
{
public:
    static const char* const kTitle;

    void Draw(EditorContext& context, StreamingManager& streaming);

private:
    void DrawTransformSection(EditorContext& context);
    void DrawScriptSection(EditorContext& context, StreamingManager& streaming);
    void DrawMaterialSection(EditorContext& context, StreamingManager& streaming);
    void DrawTextureGrid(StreamingManager& streaming, const ModelMaterial& material, uint32 requestId, int32 materialIndex);
};
