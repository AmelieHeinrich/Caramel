/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <optional>

#include "ShaderTypes.hpp"

std::optional<ParsedShaderSource> ParseShaderFile(const String& shaderPath);
