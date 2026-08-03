/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 19:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// stb_image, stb_image_resize2, and cgltf are header-only libraries -- their implementation
// must be compiled exactly once across the whole link. This is that one translation unit.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
