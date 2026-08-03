/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Texture.hpp"

#include <Caramel/Core/FileStream.hpp>
#include <Caramel/Core/Logger.hpp>

#include <algorithm>

bool CPUTexture::Load(const String& path)
{
    m_Path = path;
    m_Mips.Clear();

    FileStream stream(path);

    CaramelAsset::CtexHeader header;
    if (stream.Read(header) != sizeof(header))
    {
        CARAMEL_ERROR("CPUTexture: failed to open or read header from '{}'", path.CStr());
        return false;
    }

    if (header.magic != CaramelAsset::kCtexMagic || header.mipCount == 0)
    {
        CARAMEL_ERROR("CPUTexture: '{}' is not a valid .ctex file", path.CStr());
        return false;
    }

    m_Width = header.width;
    m_Height = header.height;
    m_Format = header.format;
    m_Flags = header.flags;

    m_Mips.Resize(header.mipCount);

    uint32 mipWidth = header.width;
    uint32 mipHeight = header.height;
    for (uint32 i = 0; i < header.mipCount; i++)
    {
        CaramelAsset::CtexMipEntry entry;
        if (stream.Read(entry) != sizeof(entry))
        {
            CARAMEL_ERROR("CPUTexture: '{}' has a truncated mip table", path.CStr());
            m_Mips.Clear();
            return false;
        }

        m_Mips[i] = TextureMip{ mipWidth, mipHeight, entry.offset, entry.size };

        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);
    }

    return true;
}

uint64 CPUTexture::LoadMip(uint32 mipIndex, void* destination) const
{
    const TextureMip& mip = m_Mips[mipIndex];
    if (mip.size == 0)
        return 0;

    FileStream stream(m_Path);
    stream.Seek(mip.offset);
    return stream.Read(destination, mip.size);
}
