/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <optional>

#include "ShaderTypes.hpp"

// Reads `shaderPath`, recursively inlines every `#include "relative/to/including/file"` line
// (resolved relative to the including file's own directory), records every absolute file path
// touched (the root file plus all transitive includes) as the shader's dependency set, then scans
// the fully-inlined text for `#pragma (vertex|pixel|compute|task|mesh) <Entry>` and
// `#pragma variant <Name>` directives. Matched pragma lines are blanked out (not deleted) so DXC
// compile-error line numbers still map back to the original source.
//
// Returns std::nullopt if the root file (or any file it includes) can't be read -- callers must
// leave prior state untouched in that case, never swapping in a broken/unparseable shader.
std::optional<ParsedShaderSource> ParseShaderFile(const String& shaderPath);
