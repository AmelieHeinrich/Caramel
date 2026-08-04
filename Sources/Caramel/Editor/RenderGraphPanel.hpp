/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

struct EditorContext;
class Renderer;

class RenderGraphPanel
{
public:
    // Shows last frame's compiled RenderGraph -- the graph compiles/executes inside Renderer::Render(),
    // which runs after every panel's Draw() this frame, so there is no "this frame's graph" to show yet.
    void Draw(EditorContext& context, Renderer& renderer);
};
