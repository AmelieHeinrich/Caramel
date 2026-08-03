/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 14:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Depth-tested debug geometry -- occluded by scene geometry. Body lives in Common/DebugDraw.hlsli;
// this file exists only to key a second pipeline template (ShaderServer registers one template per
// shader path, and the two depth states can't share one). See DebugDrawOverlay.hlsl.

#pragma vertex DebugVS
#pragma pixel DebugPS
#pragma variant DEBUG_LINE
#pragma variant DEBUG_POINT

#include "Common/DebugDraw.hlsli"
