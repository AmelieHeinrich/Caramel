/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 08:18:22
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "FileStream.hpp"

FileStream::FileStream(const String& path)
    : m_Stream(path, std::ios::binary), m_Size(0), m_Position(0)
{
}

FileStream::~FileStream()
{
    m_Stream.close();
}

uint64 FileStream::Read(void* buffer, uint64 size)
{
    if (!m_Stream.is_open())
        return 0;

    m_Stream.read(static_cast<char*>(buffer), size);
    uint64 bytesRead = m_Stream.gcount();
    m_Position += bytesRead;
    return bytesRead;
}

void FileStream::Seek(uint64 position)
{
    if (!m_Stream.is_open())
        return;

    m_Stream.seekg(position, std::ios::beg);
    m_Position = position;
}

uint64 FileStream::Tell() const
{
    return m_Position;
}
