/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <CaramelAsset/Format.hpp>

struct TextureMip
{
    uint32 width = 0;
    uint32 height = 0;
    uint64 offset = 0;
    uint64 size = 0;
};

class CPUTexture
{
public:
    CPUTexture() = default;
    explicit CPUTexture(const String& path) { Load(path); }

    bool Load(const String& path);

    bool IsValid() const { return !m_Mips.IsEmpty(); }

    uint32 GetWidth() const { return m_Width; }
    uint32 GetHeight() const { return m_Height; }
    uint32 GetMipCount() const { return static_cast<uint32>(m_Mips.Size()); }
    CaramelAsset::ECompressedTextureFormat GetFormat() const { return m_Format; }
    bool IsSRGB() const { return (m_Flags & CaramelAsset::kCtexFlagSRGB) != 0; }

    const TextureMip& GetMip(uint32 mipIndex) const { return m_Mips[mipIndex]; }

    uint64 LoadMip(uint32 mipIndex, void* destination) const;

private:
    String m_Path;
    uint32 m_Width = 0;
    uint32 m_Height = 0;
    CaramelAsset::ECompressedTextureFormat m_Format = CaramelAsset::ECompressedTextureFormat::BC7Unorm;
    uint32 m_Flags = 0;

    TArray<TextureMip> m_Mips;
};
