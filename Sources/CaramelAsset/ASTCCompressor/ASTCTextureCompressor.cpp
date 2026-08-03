/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:56:15
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ASTCTextureCompressor.hpp"

#include <astcenc.h>

namespace CaramelAsset
{
    ECompressedTextureFormat PlatformTextureBackend::SelectFormat(ETextureRole role)
    {
        switch (role)
        {
            case ETextureRole::BaseColor:
            case ETextureRole::Emissive:
                return ECompressedTextureFormat::ASTC4x4UnormSRGB;
            case ETextureRole::Normal:
            case ETextureRole::MetallicRoughness:
            case ETextureRole::Occlusion:
            case ETextureRole::ORM:
            case ETextureRole::Generic:
            case ETextureRole::HDR: // wired, unexercised: pipeline only ever decodes LDR RGBA8 source images
            default:
                return ECompressedTextureFormat::ASTC4x4Unorm;
        }
    }

    bool PlatformTextureBackend::IsSRGB(ETextureRole role)
    {
        return role == ETextureRole::BaseColor || role == ETextureRole::Emissive;
    }

    TArray<uint8> PlatformTextureBackend::CompressMip(const uint8* pixelsRGBA8, uint32 width, uint32 height, ECompressedTextureFormat format)
    {
        const bool isSRGB = (format == ECompressedTextureFormat::ASTC4x4UnormSRGB || format == ECompressedTextureFormat::ASTC8x8UnormSRGB);
        const astcenc_profile profile = isSRGB ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR;

        astcenc_config config{};
        if (astcenc_config_init(profile, 4, 4, 1, ASTCENC_PRE_MEDIUM, 0, &config) != ASTCENC_SUCCESS)
            return {};

        astcenc_context* context = nullptr;
        if (astcenc_context_alloc(&config, 1, &context) != ASTCENC_SUCCESS)
            return {};

        astcenc_image image{};
        image.dim_x = width;
        image.dim_y = height;
        image.dim_z = 1;
        image.data_type = ASTCENC_TYPE_U8;
        uint8_t* slices[1] = { const_cast<uint8_t*>(pixelsRGBA8) };
        image.data = reinterpret_cast<void**>(slices);

        const astcenc_swizzle swizzle{ ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };

        const uint32 blocksX = (width + 3) / 4;
        const uint32 blocksY = (height + 3) / 4;
        TArray<uint8> output(static_cast<size_t>(blocksX) * blocksY * 16);

        astcenc_error result = astcenc_compress_image(context, &image, &swizzle, output.Data(), output.Size(), 0);

        astcenc_context_free(context);

        if (result != ASTCENC_SUCCESS)
            return {};

        return output;
    }
}
