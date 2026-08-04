/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:54:24
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "TextureCompressor.hpp"

#include <stb_image_resize2.h>

#include <glm/glm.hpp>

#include <algorithm>

namespace CaramelAsset
{
    TArray<TArray<uint8>> TextureCompressor::GenerateMipChainRGBA8(const uint8* pixelsRGBA8, uint32 width, uint32 height, bool isSRGB, bool isNormalMap, TArray<uint32>& outMipWidths, TArray<uint32>& outMipHeights)
    {
        TArray<TArray<uint8>> mips;

        uint32 w = width, h = height;
        TArray<uint8> current(pixelsRGBA8, pixelsRGBA8 + static_cast<size_t>(width) * height * 4);

        mips.PushBack(current);
        outMipWidths.PushBack(w);
        outMipHeights.PushBack(h);

        while (w > 1 || h > 1)
        {
            uint32 nextWidth = std::max(1u, w / 2);
            uint32 nextHeight = std::max(1u, h / 2);

            TArray<uint8> next(static_cast<size_t>(nextWidth) * nextHeight * 4);

            if (isSRGB)
                stbir_resize_uint8_srgb(current.Data(), static_cast<int>(w), static_cast<int>(h), 0, next.Data(), static_cast<int>(nextWidth), static_cast<int>(nextHeight), 0, STBIR_RGBA);
            else
                stbir_resize_uint8_linear(current.Data(), static_cast<int>(w), static_cast<int>(h), 0, next.Data(), static_cast<int>(nextWidth), static_cast<int>(nextHeight), 0, STBIR_RGBA);

            if (isNormalMap)
            {
                for (size_t i = 0; i < static_cast<size_t>(nextWidth) * nextHeight; i++)
                {
                    uint8* p = &next[i * 4];
                    glm::vec3 n(p[0] / 255.0f * 2.0f - 1.0f, p[1] / 255.0f * 2.0f - 1.0f, p[2] / 255.0f * 2.0f - 1.0f);
                    float32 len = glm::length(n);
                    n = (len > 1e-6f) ? (n / len) : glm::vec3(0.0f, 0.0f, 1.0f);
                    p[0] = static_cast<uint8>(std::clamp((n.x * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    p[1] = static_cast<uint8>(std::clamp((n.y * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    p[2] = static_cast<uint8>(std::clamp((n.z * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                }
            }

            mips.PushBack(next);
            outMipWidths.PushBack(nextWidth);
            outMipHeights.PushBack(nextHeight);

            current = std::move(next);
            w = nextWidth;
            h = nextHeight;
        }

        return mips;
    }

    CompiledTexture TextureCompressor::Compile(const uint8* pixelsRGBA8, uint32 width, uint32 height, ETextureRole role)
    {
        CompiledTexture result;
        result.width = width;
        result.height = height;
        result.format = PlatformTextureBackend::SelectFormat(role);
        result.isSRGB = PlatformTextureBackend::IsSRGB(role);

        TArray<uint32> mipWidths, mipHeights;
        TArray<TArray<uint8>> mipPixels = GenerateMipChainRGBA8(pixelsRGBA8, width, height, result.isSRGB, role == ETextureRole::Normal, mipWidths, mipHeights);

        result.mips.Reserve(mipPixels.Size());
        for (size_t i = 0; i < mipPixels.Size(); i++)
            result.mips.PushBack(PlatformTextureBackend::CompressMip(mipPixels[i].Data(), mipWidths[i], mipHeights[i], result.format));

        return result;
    }
}
