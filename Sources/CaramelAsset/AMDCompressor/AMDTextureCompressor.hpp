/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:56:25
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

// Windows/Linux texture backend: implements CaramelAsset::PlatformTextureBackend (declared in
// TextureCompressor.hpp) on top of AMD Compressonator's CMP_Core per-block BCn encoder.
#include "../TextureCompressor.hpp"
