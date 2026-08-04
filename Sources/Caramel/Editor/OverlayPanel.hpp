/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:02:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

struct SDL_Window;
struct agfxDeviceInfo;
class StreamingManager;
struct EditorContext;

class OverlayPanel
{
public:
    void Draw(EditorContext& context, StreamingManager& streaming, SDL_Window* window, const agfxDeviceInfo& deviceInfo, bool& showColliders);
};
