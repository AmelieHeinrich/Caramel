/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 19:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <glm/glm.hpp>
#include <AGFX/agfx.hpp>

#include <algorithm>

namespace CaramelAsset
{
    constexpr uint32 kCmdlMagic = 0x4C444D43; // 'CMDL'
    constexpr uint32 kCtexMagic = 0x58455443; // 'CTEX'
    constexpr uint32 kFormatVersion = 1;

    constexpr uint32 kLodCount = 5;
    constexpr uint32 kMeshletMaxVertices = 64;
    constexpr uint32 kMeshletMaxTriangles = 124;
    constexpr float32 kMeshletConeWeight = 0.5f;

    //
    // .cmdl container: CmdlHeader, then `jsonChunkSize` bytes of UTF-8 JSON (space-padded to
    // a 4-byte boundary), then `binChunkSize` bytes of raw binary (zero-padded to a 16-byte
    // boundary so vertex/meshlet reads can be SIMD-aligned).
    //
    struct CmdlHeader
    {
        uint32 magic = kCmdlMagic;
        uint32 version = kFormatVersion;
        uint32 jsonChunkSize = 0;
        uint32 binChunkSize = 0;
    };

    // Interleaved mesh vertex. Shared across all LODs of a mesh -- meshopt_simplify only
    // reduces the index buffer, it never rebuilds the vertex buffer.
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent; // xyz = tangent, w = handedness (+1/-1)
        glm::vec2 uv;
    };

    // Present only for skinned meshes, one entry per Vertex, same indexing.
    struct SkinVertex
    {
        uint16 jointIndices[4];
        float32 jointWeights[4];
    };

    // Mirrors meshopt_Meshlet exactly so it can be memcpy'd straight from meshoptimizer output.
    struct MeshletDesc
    {
        uint32 vertexOffset;
        uint32 triangleOffset;
        uint32 vertexCount;
        uint32 triangleCount;
    };

    // Mirrors meshopt_Bounds (the fields we use) so it can be memcpy'd straight from meshoptimizer output.
    struct MeshletCullData
    {
        glm::vec3 center;
        float32 radius;
        glm::vec3 coneApex;
        glm::vec3 coneAxis;
        float32 coneCutoff;
    };

    //
    // .ctex container: CtexHeader, then `mipCount` MipEntry records, then the raw compressed
    // block data for each mip back-to-back (each mip start padded to a 4-byte boundary).
    //
    enum class ECompressedTextureFormat : uint32
    {
        BC4Unorm,
        BC5Unorm,
        BC6HUFloat,
        BC7Unorm,
        BC7UnormSRGB,
        ASTC4x4Unorm,
        ASTC4x4UnormSRGB,
        ASTC8x8Unorm,
        ASTC8x8UnormSRGB,
    };

    inline agfx::TextureFormat ToAgfxFormat(ECompressedTextureFormat format)
    {
        switch (format)
        {
        case ECompressedTextureFormat::BC4Unorm: return agfx::TextureFormat::BC4Unorm;
        case ECompressedTextureFormat::BC5Unorm: return agfx::TextureFormat::BC5Unorm;
        case ECompressedTextureFormat::BC6HUFloat: return agfx::TextureFormat::BC6HUFloat;
        case ECompressedTextureFormat::BC7Unorm: return agfx::TextureFormat::BC7Unorm;
        case ECompressedTextureFormat::BC7UnormSRGB: return agfx::TextureFormat::BC7UnormSRGB;
        case ECompressedTextureFormat::ASTC4x4Unorm: return agfx::TextureFormat::ASTC4x4Unorm;
        case ECompressedTextureFormat::ASTC4x4UnormSRGB: return agfx::TextureFormat::ASTC4x4UnormSRGB;
        case ECompressedTextureFormat::ASTC8x8Unorm: return agfx::TextureFormat::ASTC8x8Unorm;
        case ECompressedTextureFormat::ASTC8x8UnormSRGB: return agfx::TextureFormat::ASTC8x8UnormSRGB;
        default:
            return agfx::TextureFormat::Unknown;
        }
    }

    // Block dimensions (in texels) and compressed byte size of a single block, for computing
    // bytesPerRow/bytesPerImage when uploading a mip's raw compressed bytes to the GPU.
    struct BlockInfo
    {
        uint32 width = 1;
        uint32 height = 1;
        uint32 bytesPerBlock = 1;
    };

    inline BlockInfo GetBlockInfo(ECompressedTextureFormat format)
    {
        switch (format)
        {
        case ECompressedTextureFormat::BC4Unorm: return BlockInfo{ 4, 4, 8 };
        case ECompressedTextureFormat::BC5Unorm: return BlockInfo{ 4, 4, 16 };
        case ECompressedTextureFormat::BC6HUFloat: return BlockInfo{ 4, 4, 16 };
        case ECompressedTextureFormat::BC7Unorm: return BlockInfo{ 4, 4, 16 };
        case ECompressedTextureFormat::BC7UnormSRGB: return BlockInfo{ 4, 4, 16 };
        case ECompressedTextureFormat::ASTC4x4Unorm: return BlockInfo{ 4, 4, 16 };
        case ECompressedTextureFormat::ASTC4x4UnormSRGB: return BlockInfo{ 4, 4, 16 };
        case ECompressedTextureFormat::ASTC8x8Unorm: return BlockInfo{ 8, 8, 16 };
        case ECompressedTextureFormat::ASTC8x8UnormSRGB: return BlockInfo{ 8, 8, 16 };
        default:
            return BlockInfo{};
        }
    }

    // Bytes spanned by one row of blocks for a mip of the given pixel width.
    inline uint32 GetBytesPerRow(ECompressedTextureFormat format, uint32 mipWidth)
    {
        BlockInfo block = GetBlockInfo(format);
        uint32 blocksPerRow = (mipWidth + block.width - 1) / block.width;
        return blocksPerRow * block.bytesPerBlock;
    }

    inline void GetBlockAlignedExtent(ECompressedTextureFormat format, uint32 width, uint32 height, uint32& outWidth, uint32& outHeight)
    {
        BlockInfo block = GetBlockInfo(format);
        outWidth = std::min(((width + block.width - 1) / block.width) * block.width, width);
        outHeight = std::min(((height + block.height - 1) / block.height) * block.height, height);
    }

    enum ECtexFlags : uint32
    {
        kCtexFlagSRGB = 1 << 0,
    };

    // How a source glTF texture is used -- determines target compressed format and color space.
    enum class ETextureRole
    {
        BaseColor,
        Normal,
        MetallicRoughness,
        Occlusion,
        ORM,
        Emissive,
        Generic,
        HDR,
    };

    struct CtexHeader
    {
        uint32 magic = kCtexMagic;
        uint32 version = kFormatVersion;
        uint32 width = 0;
        uint32 height = 0;
        ECompressedTextureFormat format = ECompressedTextureFormat::BC7Unorm;
        uint32 mipCount = 0;
        uint32 arrayLayers = 1;
        uint32 flags = 0;
    };

    struct CtexMipEntry
    {
        uint64 offset = 0;
        uint64 size = 0;
    };
}
