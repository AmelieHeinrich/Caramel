/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/CpuProfiler.hpp>

class CpuProfilerPanel
{
public:
    void Draw();

private:
    float m_PixelsPerMs = 120.0f;
    bool m_Paused = false;

    // The frame currently on screen. Refreshed every Draw() unless paused, in which case it just
    // keeps showing whatever it held the moment Pause was pressed -- the live profiler keeps
    // recording underneath regardless, this only freezes the display.
    TArray<CpuProfileThreadSnapshot> m_DisplaySnapshot;
};
