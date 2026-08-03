/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 08:17:11
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <fstream>

class FileStream
{
public:
    FileStream(const String& path);
    ~FileStream();

    uint64 Read(void* buffer, uint64 size);
    
    template<typename T>
    uint64 Read(T& value)
    {
        return Read(&value, sizeof(T));
    }

    void Seek(uint64 position);
    uint64 Tell() const;
    
private:
    std::ifstream m_Stream;
    uint64 m_Size;
    uint64 m_Position;
};
