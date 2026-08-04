/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <chrono>

class Timer
{
public:
    Timer();

    void Tick();

    float32 GetDelta() const { return m_Delta; }
    float32 GetElapsed() const { return m_Elapsed; }
    uint64 GetFrameCount() const { return m_FrameCount; }

    static constexpr float32 kMaxDelta = 0.25f;

private:
    std::chrono::steady_clock::time_point m_Start;
    std::chrono::steady_clock::time_point m_Last;
    float32 m_Delta = 0.0f;
    float32 m_Elapsed = 0.0f;
    uint64 m_FrameCount = 0;
};
