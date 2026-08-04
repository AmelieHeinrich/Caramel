/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:56:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "AMDTextureCompressor.hpp"

#include <cmp_core.h>

#include <algorithm>
#include <cstring>

namespace CaramelAsset
{
    namespace
    {
        uint32 BlockByteSize(ECompressedTextureFormat format)
        {
            switch (format)
            {
                case ECompressedTextureFormat::BC4Unorm: return 8;
                default: return 16;
            }
        }

        TArray<uint8> BuildClampPaddedRGBA(const uint8* pixelsRGBA8, uint32 width, uint32 height, uint32 paddedWidth, uint32 paddedHeight)
        {
            TArray<uint8> padded(static_cast<size_t>(paddedWidth) * paddedHeight * 4);
            for (uint32 y = 0; y < paddedHeight; y++)
            {
                uint32 sy = std::min(y, height - 1);
                for (uint32 x = 0; x < paddedWidth; x++)
                {
                    uint32 sx = std::min(x, width - 1);
                    std::memcpy(&padded[(static_cast<size_t>(y) * paddedWidth + x) * 4], &pixelsRGBA8[(static_cast<size_t>(sy) * width + sx) * 4], 4);
                }
            }
            return padded;
        }

        TArray<uint8> ExtractChannelPlane(const uint8* rgba, uint32 width, uint32 height, uint32 channel)
        {
            TArray<uint8> plane(static_cast<size_t>(width) * height);
            for (size_t i = 0; i < plane.Size(); i++)
                plane[i] = rgba[i * 4 + channel];
            return plane;
        }
    }

    ECompressedTextureFormat PlatformTextureBackend::SelectFormat(ETextureRole role)
    {
        switch (role)
        {
            case ETextureRole::BaseColor:
            case ETextureRole::Emissive:
                return ECompressedTextureFormat::BC7UnormSRGB;
            case ETextureRole::Normal:
            case ETextureRole::MetallicRoughness:
                return ECompressedTextureFormat::BC5Unorm;
            case ETextureRole::Occlusion:
                return ECompressedTextureFormat::BC4Unorm;
            case ETextureRole::ORM:
                return ECompressedTextureFormat::BC7Unorm;
            case ETextureRole::HDR:
                return ECompressedTextureFormat::BC6HUFloat;
            case ETextureRole::Generic:
            default:
                return ECompressedTextureFormat::BC7Unorm;
        }
    }

    bool PlatformTextureBackend::IsSRGB(ETextureRole role)
    {
        return role == ETextureRole::BaseColor || role == ETextureRole::Emissive;
    }

    TArray<uint8> PlatformTextureBackend::CompressMip(const uint8* pixelsRGBA8, uint32 width, uint32 height, ECompressedTextureFormat format)
    {
        const uint32 blocksX = (width + 3) / 4;
        const uint32 blocksY = (height + 3) / 4;
        const uint32 paddedWidth = blocksX * 4;
        const uint32 paddedHeight = blocksY * 4;

        TArray<uint8> paddedRGBA = BuildClampPaddedRGBA(pixelsRGBA8, width, height, paddedWidth, paddedHeight);

        TArray<uint8> planeR, planeG;
        if (format == ECompressedTextureFormat::BC4Unorm || format == ECompressedTextureFormat::BC5Unorm)
        {
            planeR = ExtractChannelPlane(paddedRGBA.Data(), paddedWidth, paddedHeight, 0);
            if (format == ECompressedTextureFormat::BC5Unorm)
                planeG = ExtractChannelPlane(paddedRGBA.Data(), paddedWidth, paddedHeight, 1);
        }

        const uint32 blockBytes = BlockByteSize(format);
        TArray<uint8> output(static_cast<size_t>(blocksX) * blocksY * blockBytes);

        void* bc7Options = nullptr;
        if (format == ECompressedTextureFormat::BC7Unorm || format == ECompressedTextureFormat::BC7UnormSRGB)
        {
            CreateOptionsBC7(&bc7Options);
            SetQualityBC7(bc7Options, 0.2f);
        }

        for (uint32 by = 0; by < blocksY; by++)
        {
            for (uint32 bx = 0; bx < blocksX; bx++)
            {
                uint8* out = &output[(static_cast<size_t>(by) * blocksX + bx) * blockBytes];

                switch (format)
                {
                    case ECompressedTextureFormat::BC7Unorm:
                    case ECompressedTextureFormat::BC7UnormSRGB:
                    {
                        const uint8* block = &paddedRGBA[(static_cast<size_t>(by) * 4 * paddedWidth + static_cast<size_t>(bx) * 4) * 4];
                        CompressBlockBC7(block, paddedWidth * 4, out, bc7Options);
                        break;
                    }
                    case ECompressedTextureFormat::BC5Unorm:
                    {
                        const uint8* blockR = &planeR[static_cast<size_t>(by) * 4 * paddedWidth + static_cast<size_t>(bx) * 4];
                        const uint8* blockG = &planeG[static_cast<size_t>(by) * 4 * paddedWidth + static_cast<size_t>(bx) * 4];
                        CompressBlockBC5(blockR, paddedWidth, blockG, paddedWidth, out);
                        break;
                    }
                    case ECompressedTextureFormat::BC4Unorm:
                    {
                        const uint8* block = &planeR[static_cast<size_t>(by) * 4 * paddedWidth + static_cast<size_t>(bx) * 4];
                        CompressBlockBC4(block, paddedWidth, out);
                        break;
                    }
                    default:
                        std::memset(out, 0, blockBytes);
                        break;
                }
            }
        }

        if (bc7Options)
            DestroyOptionsBC7(bc7Options);

        return output;
    }
}
