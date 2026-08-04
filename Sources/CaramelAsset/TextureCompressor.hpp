/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:54:19
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include "Format.hpp"

namespace CaramelAsset
{
    struct CompiledTexture
    {
        uint32 width = 0;
        uint32 height = 0;
        ECompressedTextureFormat format = ECompressedTextureFormat::BC7Unorm;
        bool isSRGB = false;
        TArray<TArray<uint8>> mips;
    };

    namespace PlatformTextureBackend
    {
        ECompressedTextureFormat SelectFormat(ETextureRole role);
        bool IsSRGB(ETextureRole role);
        TArray<uint8> CompressMip(const uint8* pixelsRGBA8, uint32 width, uint32 height, ECompressedTextureFormat format);
    }

    class TextureCompressor
    {
    public:
        static CompiledTexture Compile(const uint8* pixelsRGBA8, uint32 width, uint32 height, ETextureRole role);

    private:
        static TArray<TArray<uint8>> GenerateMipChainRGBA8(const uint8* pixelsRGBA8, uint32 width, uint32 height, bool isSRGB, bool isNormalMap, TArray<uint32>& outMipWidths, TArray<uint32>& outMipHeights);
    };
}
