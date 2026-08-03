/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:56:10
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

// macOS texture backend: implements CaramelAsset::PlatformTextureBackend (declared in
// TextureCompressor.hpp) on top of ARM astc-encoder, using a fixed 4x4 block size.
#include "../TextureCompressor.hpp"
