/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Same shader as VisBuffer.hlsl, registered under its own path so it can carry a different raster
// state: ShaderServer keys one pipeline template per shader path, and the double-sided bundle
// region needs CullMode::None where the single-sided region uses CullMode::Back.

#include "VisBuffer.hlsl"
