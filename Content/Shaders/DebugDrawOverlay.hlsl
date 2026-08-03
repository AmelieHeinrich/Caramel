/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 14:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Always-on-top debug geometry -- no depth attachment, so nothing occludes it. Body lives in
// Common/DebugDraw.hlsli; see DebugDrawDepth.hlsl for why this is a separate file.

#pragma vertex DebugVS
#pragma pixel DebugPS
#pragma variant DEBUG_LINE
#pragma variant DEBUG_POINT

#include "Common/DebugDraw.hlsli"
