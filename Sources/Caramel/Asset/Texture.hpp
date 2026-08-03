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
    uint64 offset = 0; // absolute, from the start of the file
    uint64 size = 0;   // compressed byte size, no padding included
};

// Parses a compiled .ctex file's header + mip table up front (32 + 16*mipCount bytes) and exposes
// on-demand, per-mip reads of the compressed block data. Each LoadMip() call is self-contained
// (opens its own file handle) so it's safe to fire off from any thread, e.g. one JobSystem job per
// mip, letting a caller stream in exactly the mips it currently wants. Does no GPU work.
class CPUTexture
{
public:
    CPUTexture() = default;
    // Convenience constructor equivalent to CPUTexture{} + Load(path); check IsValid() to see
    // whether it succeeded.
    explicit CPUTexture(const String& path) { Load(path); }

    // Opens `path` and parses the header + mip table. Returns false, leaving the asset empty, on
    // a missing file or a bad magic/empty mip table.
    bool Load(const String& path);

    bool IsValid() const { return !m_Mips.IsEmpty(); }

    uint32 GetWidth() const { return m_Width; }
    uint32 GetHeight() const { return m_Height; }
    uint32 GetMipCount() const { return static_cast<uint32>(m_Mips.Size()); }
    CaramelAsset::ECompressedTextureFormat GetFormat() const { return m_Format; }
    bool IsSRGB() const { return (m_Flags & CaramelAsset::kCtexFlagSRGB) != 0; }

    const TextureMip& GetMip(uint32 mipIndex) const { return m_Mips[mipIndex]; }

    // Reads mip[mipIndex]'s compressed block data into `destination`, which must be at least
    // GetMip(mipIndex).size bytes. Returns the number of bytes actually read (0 on failure).
    uint64 LoadMip(uint32 mipIndex, void* destination) const;

private:
    String m_Path;
    uint32 m_Width = 0;
    uint32 m_Height = 0;
    CaramelAsset::ECompressedTextureFormat m_Format = CaramelAsset::ECompressedTextureFormat::BC7Unorm;
    uint32 m_Flags = 0;

    TArray<TextureMip> m_Mips;
};
