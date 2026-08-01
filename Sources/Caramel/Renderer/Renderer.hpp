/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 21:25:29
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <AGFX/agfx.hpp>

class Renderer
{
public:
    Renderer();
    ~Renderer();

private:
    agfx::Device m_Device;

    static void* Allocate(uint64 size);
    static void Free(void* ptr);
    static void* TempAllocate(uint64 size);
    static void TempFree(void* ptr);
    static void Log(agfxLogSeverity level, const char* message);
};
