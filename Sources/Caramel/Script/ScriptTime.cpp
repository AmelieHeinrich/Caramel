/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include <Caramel/Script/ScriptTime.hpp>

float32 ScriptTime::s_Delta = 0.0f;
float32 ScriptTime::s_Elapsed = 0.0f;
uint64 ScriptTime::s_FrameCount = 0;

void ScriptTime::Set(float32 delta, float32 elapsed, uint64 frameCount)
{
    s_Delta = delta;
    s_Elapsed = elapsed;
    s_FrameCount = frameCount;
}
