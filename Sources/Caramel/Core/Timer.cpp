/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Core/Timer.hpp>

Timer::Timer()
{
    m_Start = std::chrono::steady_clock::now();
    m_Last = m_Start;
}

void Timer::Tick()
{
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    float32 delta = std::chrono::duration<float32>(now - m_Last).count();
    if (delta > kMaxDelta)
        delta = kMaxDelta;

    m_Delta = delta;
    m_Elapsed = std::chrono::duration<float32>(now - m_Start).count();
    m_Last = now;
    m_FrameCount++;
}
